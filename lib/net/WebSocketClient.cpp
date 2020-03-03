#include <net/WebSocketClient.h>
#include <net/UriParser.h>
#include <net/UriBuilder.h>
#include <wslay/wslay.h>
#include <buffer/Pool.h>
#include <log/Log.h>
#include <util/Random.h>
#include <util/Base64.h>
#include <util/Socket.h>
#include <openssl/sha.h>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;

WebSocketClient::WebSocketClient(const std::string& url)
    : mBufferOffset(0), mCtx(nullptr), mUpgraded(false), mDupped(-1), mFdWriteOffset(0)
{
    uint8_t clientKey[16];
    uint8_t encodedClientKey[25];
    util::Random::random().fill(clientKey, sizeof(clientKey));
    const size_t encodedLength = util::base64::encode(clientKey, sizeof(clientKey), encodedClientKey, sizeof(encodedClientKey));
    assert(encodedLength > sizeof(clientKey) && encodedLength < sizeof(encodedClientKey));
    encodedClientKey[encodedLength] = '\0';

    auto recvCallback = [](wslay_event_context_ptr ctx, uint8_t* data, size_t len, int flags, void* userData) -> ssize_t {
        WebSocketClient* ws = static_cast<WebSocketClient*>(userData);
        if (!ws->mHttp) {
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
                ws->mReadBuffers.pop();
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
        if (!ws->mHttp) {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
            return 0;
        }

        // if there's stuff in our pending queue, add to it
        if (!ws->mFdWriteBuffers.empty()) {
            auto buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get(len);
            buf->assign(data, len);
            ws->mFdWriteBuffers.push(buf);
            return len;
        }

        int e;
        if (ws->mDupped == -1) {
            eintrwrap(e, ::dup(ws->mHttp->fd()));
            if (e > 0) {
                ws->mDupped = e;
            } else {
                // horrible badness
                return 0;
            }
        }
        const int fd = ws->mDupped;
        int rem = len;
        int off = 0;
        for (;;) {
            eintrwrap(e, ::write(fd, data + off, rem));
            if (e > 0) {
                rem -= e;
                off += e;
                if (!rem) {
                    // all done
                    return len;
                }
            } else if (e == EAGAIN || e == EWOULDBLOCK) {
                // retry later
                auto buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get(rem);
                buf->assign(data + off, rem);
                ws->mFdWriteBuffers.push(buf);

                // tell our event loop that we want to know about write availability
                auto loop = event::Loop::loop();

                std::function<void(int, uint8_t)> onwrite;
                onwrite = [ws, &onwrite](int fd, uint8_t flags) {
                    if (flags & event::Loop::FdWrite) {
                        auto innerLoop = event::Loop::loop();
                        innerLoop->removeFd(fd);
                        // write some more
                        int where = ws->mFdWriteOffset;
                        int e;
                        while (!ws->mFdWriteBuffers.empty()) {
                            const auto& buffer = ws->mFdWriteBuffers.front();
                            eintrwrap(e, ::write(fd, buffer->data() + where, buffer->size() - where));
                            if (e > 0) {
                                where += e;
                                if (where == buffer->size()) {
                                    ws->mFdWriteBuffers.pop();
                                    where = 0;
                                }
                            } else if (e == EAGAIN || e == EWOULDBLOCK) {
                                // boo
                                innerLoop->addFd(fd, event::Loop::FdWrite, onwrite);
                                break;
                            }
                        }
                        ws->mFdWriteOffset = where;
                    }
                };

                loop->addFd(fd, event::Loop::FdWrite, onwrite);
                break;
            }
        }
        return len;
    };

    auto msgrcvCallback = [](wslay_event_context_ptr ctx, const struct wslay_event_on_msg_recv_arg* arg, void* userData) {
        if (wslay_is_ctrl_frame(arg->opcode)) {
            // we don't deal with control frames yet
            return;
        }
        WebSocketClient* ws = static_cast<WebSocketClient*>(userData);
        if (!ws->mHttp) {
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
    headers.add("Sec-WebSocket-Key", reinterpret_cast<char*>(encodedClientKey));
    headers.add("Sec-WebSocket-Version", "13");

    std::string scheme;
    if (uri.scheme() == "ws") {
        scheme = "http";
    } else if (uri.scheme() == "wss") {
        scheme = "https";
    } else {
        // invalid scheme maybe?
        scheme = uri.scheme();
    }

    const std::string httpUrl = buildUri(scheme, uri.host(), uri.port(), uri.path(), uri.query(), uri.fragment());
    printf("translated uri '%s'\n", httpUrl.c_str());

    mHttp = HttpClient::create(httpUrl, headers);

    mHttp->onResponse().connect([this, encodedClientKey, encodedLength](HttpClient::Response&& response) {
        // verify accept key
        const auto& accept = response.headers.find("sec-websocket-accept");
        if (accept.empty()) {
            // welp
            Log(Log::Error) << "No websocket accept key found";
            if (mCtx) {
                wslay_event_context_free(mCtx);
                mCtx = nullptr;
            }
            mHttp.reset();
        }

        static const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        uint8_t digest[SHA_DIGEST_LENGTH];
        SHA_CTX sha1ctx;
        SHA1_Init(&sha1ctx);
        SHA1_Update(&sha1ctx, encodedClientKey, encodedLength);
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
            mHttp.reset();
            return;
        }
        // we good
        mUpgraded = true;
        write();
    });
    mHttp->onComplete().connect([this]() {
        if (mDupped != -1) {
            int e;
            eintrwrap(e, ::close(mDupped));
            mDupped = -1;
        }
        if (mCtx) {
            wslay_event_context_free(mCtx);
            mCtx = nullptr;
        }
        mHttp.reset();
        mComplete.emit();
    });
    mHttp->onError().connect([this](std::string&& err) {
        mError.emit(std::move(err));
    });
    mHttp->onBodyData().connect([this](std::shared_ptr<buffer::Buffer>&& buffer) {
        if (!mUpgraded || !wslay_event_want_read(mCtx))
            return;
        //mData.emit(std::move(buffer));
        assert(buffer);
        mReadBuffers.push(std::move(buffer));
        while (!mReadBuffers.empty()) {
            wslay_event_recv(mCtx);
            if (!wslay_event_want_read(mCtx))
                mReadBuffers = {};
        }
    });
}

void WebSocketClient::close()
{
    if (!mHttp)
        return;
    assert(mCtx != nullptr);
    mHttp.reset();
    wslay_event_context_free(mCtx);
    mCtx = nullptr;
    mUpgraded = false;
}

void WebSocketClient::write()
{
    if (!mHttp || !mCtx || !mUpgraded)
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
        mWriteBuffers.pop();
    }
}
