#include <net/WebSocketClient.h>
#include <net/UriParser.h>
#include <net/UriBuilder.h>
#include <net/HttpClient.h>
#include <wslay/wslay.h>
#include <buffer/Pool.h>
#include <log/Log.h>
#include <util/Random.h>
#include <util/Base64.h>
#include <openssl/sha.h>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;

WebSocketClient::WebSocketClient(const std::string& url)
    : mBufferOffset(0), mCtx(nullptr), mUpgraded(false)
{
    uint8_t clientKey[16];
    std::string encodedClientKey;
    encodedClientKey.resize(24);
    util::Random::random().fill(clientKey, sizeof(clientKey));
    const size_t encodedLength = util::base64::encode(clientKey, sizeof(clientKey), reinterpret_cast<uint8_t*>(&encodedClientKey[0]), encodedClientKey.size());
    encodedClientKey.resize(encodedLength);

    mLoop = event::Loop::loop();

    auto recvCallback = [](wslay_event_context_ptr ctx, uint8_t* data, size_t len, int flags, void* userData) -> ssize_t {
        WebSocketClient* ws = static_cast<WebSocketClient*>(userData);
        if (!ws->mTcp) {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
            return -1;
        }
        if (ws->mReadBuffers.empty()) {
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
            return -1;
        }
        size_t off = 0;
        size_t rem = len;
        while (rem > 0) {
            const auto& buf = ws->mReadBuffers.front();
            const size_t toread = std::min(rem, buf->size() - ws->mBufferOffset);
            memcpy(data + off, buf->data() + ws->mBufferOffset, toread);
            rem -= toread;
            off += toread;
            if (toread == buf->size()) {
                ws->mReadBuffers.pop_front();
                ws->mBufferOffset = 0;
                if (ws->mReadBuffers.empty())
                    break;
            } else {
                ws->mBufferOffset = toread;
                assert(!rem);
                return len;
            }
        }
        return len - rem;
    };

    auto sendCallback = [](wslay_event_context_ptr ctx, const uint8_t* data, size_t len, int flags, void* userData) -> ssize_t {
        WebSocketClient* ws = static_cast<WebSocketClient*>(userData);
        if (!ws->mTcp) {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
            return 0;
        }

        ws->mTcp->write(data, len);
        return len;
    };

    auto msgrcvCallback = [](wslay_event_context_ptr ctx, const struct wslay_event_on_msg_recv_arg* arg, void* userData) {
        if (wslay_is_ctrl_frame(arg->opcode)) {
            // we don't deal with control frames yet
            return;
        }
        WebSocketClient* ws = static_cast<WebSocketClient*>(userData);
        if (!ws->mTcp) {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
            return;
        }
        auto buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get(arg->msg_length);
        buf->assign(arg->msg, arg->msg_length);
        ws->mMessage.emit(std::move(buf));
    };

    auto genmaskCallback = [](wslay_event_context_ptr ctx, uint8_t *buf, size_t len, void *userData) -> int {
        util::Random::random().fill(buf, len);
        return 0;
    };

    struct wslay_event_callbacks callbacks = {
        recvCallback,
        sendCallback,
        genmaskCallback,
        nullptr, /* on_frame_recv_start_callback */
        nullptr, /* on_frame_recv_callback */
        nullptr, /* on_frame_recv_end_callback */
        msgrcvCallback
    };

    wslay_event_context_client_init(&mCtx, &callbacks, this);

    UriParser uri(url);

    // make an upgrade request
    HttpClient::Headers headers;
    headers.add("Host", uri.host());
    headers.add("Upgrade", "websocket");
    headers.add("Connection", "upgrade");
    headers.add("Sec-WebSocket-Key", encodedClientKey);
    headers.add("Sec-WebSocket-Version", "13");

    mTcp = TcpSocket::create();

    mTcp->onData().connect([this, encodedClientKey](std::shared_ptr<buffer::Buffer>&& buffer) {
        assert(buffer);
        mReadBuffers.push_back(std::move(buffer));

        // verify accept key
        if (!mUpgraded) {
            attemptUpgrade(encodedClientKey);
        }
        if (mUpgraded) {
            // consume read buffers
            while (!mReadBuffers.empty()) {
                wslay_event_recv(mCtx);
                if (!wslay_event_want_read(mCtx))
                    break;
            }
        }
    });
    mTcp->onStateChanged().connect([this](TcpSocket::State state) {
        if (state == TcpSocket::Closed || state == TcpSocket::Error) {
            if (mCtx) {
                wslay_event_context_free(mCtx);
                mCtx = nullptr;
            }
            mTcp.reset();
            if (state == TcpSocket::Closed) {
                mComplete.emit();
            } else {
                // error
                mError.emit(std::string("tcp stream error"));
            }
        }
    });

    const std::string req = "GET " + uri::path(uri) + " HTTP/1.1\r\n" + headers.toString() + "\r\n";
    const auto port = uri.port().empty() ? (uri.scheme() == "wss" ? 443 : 80) : atoi(uri.port().c_str());
    // printf("req '%s'\n", req.c_str());
    mTcp->connect(uri.host(), port, uri.scheme() == "wss" ? TcpSocket::TLS : TcpSocket::Plain);
    mTcp->write(req);
}

void WebSocketClient::attemptUpgrade(const std::string& encodedClientKey)
{
    // see if we can parse the headers
    mergeReadBuffers();
    if (mReadBuffers.empty())
        return;
    assert(mReadBuffers.size() == 1);
    const auto& buffer = mReadBuffers[0];
    const uint8_t* mptr = static_cast<uint8_t*>(memmem(buffer->data(), buffer->size(), "\r\n\r\n", 4));
    if (!mptr)
        return;
    // we have it
    const size_t headersize = (mptr + 4) - buffer->data();

    const uint8_t* startptr = buffer->data();
    auto find = [startptr, headersize](const char* header, size_t len) -> const uint8_t* {
        auto start = startptr;
        auto rem = headersize;
        for (;;) {
            if (rem < len)
                return nullptr;
            if (!strncasecmp(reinterpret_cast<const char*>(start), header, len)) {
                // got it
                return start + len;
            }
            // find the next eol
            uint8_t* eol = static_cast<uint8_t*>(memmem(start, rem, "\r\n", 2));
            if (!eol)
                return nullptr;
            const size_t eols = (eol - start) + 2;
            assert(rem >= eols);
            if (eols == rem) {
                // done
                return nullptr;
            }
            start += eols;
            rem -= eols;
        }
    };

    // std::string headers(reinterpret_cast<const char*>(startptr), headersize);
    // printf("received headers '%s'\n", headers.c_str());

    const uint8_t* hptr = find("sec-websocket-accept: ", 22);
    // find the end
    if (!hptr) {
        // welp
        Log(Log::Error) << "No websocket accept key (start) found";
        if (mCtx) {
            wslay_event_context_free(mCtx);
            mCtx = nullptr;
        }
        mTcp.reset();
        return;
    }
    uint8_t* hend = static_cast<uint8_t*>(memmem(hptr, (buffer->data() + buffer->size()) - hptr, "\r\n", 2));
    if (!hend) {
        // shouldn't happen
        // welp
        Log(Log::Error) << "No websocket accept key (end) found";
        if (mCtx) {
            wslay_event_context_free(mCtx);
            mCtx = nullptr;
        }
        mTcp.reset();
        return;
    }
    const std::string accept(reinterpret_cast<const char*>(hptr), hend - hptr);
    // printf("accept '%s'\n", accept.c_str());

    if (buffer->size() == headersize) {
        // we've consumed the entire buffer
        mReadBuffers.clear();
    } else {
        const size_t newsize = buffer->size() - headersize;
        memmove(buffer->data(), mptr + 4, newsize);
        buffer->setSize(newsize);
    }

    static const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA_CTX sha1ctx;
    SHA1_Init(&sha1ctx);
    SHA1_Update(&sha1ctx, encodedClientKey.c_str(), encodedClientKey.size());
    SHA1_Update(&sha1ctx, magic, 36);
    SHA1_Final(digest, &sha1ctx);
    uint8_t b64digest[28];
    //memset(b64digest, '\0', sizeof(b64digest));
    size_t b64length = util::base64::encode(digest, sizeof(digest), b64digest, sizeof(b64digest));
    assert(b64length > sizeof(digest) && b64length <= sizeof(b64digest));
    if (b64length != accept.size() || memcmp(b64digest, &accept[0], b64length) != 0) {
        // failed to verify
        Log(Log::Error) << "Accept verify failed" << std::string(reinterpret_cast<char*>(b64digest), b64length) << accept;
        if (mCtx) {
            wslay_event_context_free(mCtx);
            mCtx = nullptr;
        }
        mTcp.reset();
        return;
    }
    // we good
    // printf("upgrade good\n");
    mUpgraded = true;
    write();
}

void WebSocketClient::mergeReadBuffers()
{
    if (mReadBuffers.size() < 2)
        return;
    // walk the buffer two times, one to get the total size, one more to memcpy
    size_t sz = 0;
    for (const auto& buf : mReadBuffers) {
        sz += buf->size();
    }
    if (!sz) {
        // shouldn't happen
        mReadBuffers.clear();
        return;
    }
    auto newbuf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get(sz);
    newbuf->setSize(sz);
    size_t off = 0;
    for (const auto& buf : mReadBuffers) {
        memcpy(newbuf->data(), buf->data(), buf->size());
        off += buf->size();
    }
    mReadBuffers.clear();
    mReadBuffers.push_back(newbuf);
}

void WebSocketClient::close()
{
    if (!mTcp)
        return;
    assert(mCtx != nullptr);
    mTcp.reset();
    wslay_event_context_free(mCtx);
    mCtx = nullptr;
    mUpgraded = false;
}

void WebSocketClient::write()
{
    if (!mTcp || !mCtx || !mUpgraded)
        return;
    while (!mWriteBuffers.empty()) {
        auto msg = mWriteBuffers.front();
        struct wslay_event_msg wsmsg = {
            WSLAY_TEXT_FRAME,
            msg->data(),
            msg->size()
        };
        wslay_event_queue_msg(mCtx, &wsmsg);
        while (wslay_event_want_write(mCtx)) {
            wslay_event_send(mCtx);
        }
        mWriteBuffers.pop_front();
    }
}
