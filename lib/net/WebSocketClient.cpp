#include "WebSocketClient.h"
#include <wslay/wslay.h>
#include <buffer/Pool.h>
#include <log/Log.h>
#include <util/Random.h>
#include <util/Base64.h>
#include <openssl/sha.h>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;

void WebSocketClient::connect(const std::string& host, uint16_t port, const std::string& query)
{
    if (mHttp)
        return;

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
        ws->mHttp->write(data, len);
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

    mHttp = HttpClient::create();
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
            mState = Upgraded;
            mStateChanged.emit(Upgraded);
            write();
        });
    mHttp->onBodyData().connect([this](std::shared_ptr<buffer::Buffer>&& buffer) {
            if (mState != Upgraded)
                return;
            //mData.emit(std::move(buffer));
            assert(buffer);
            mReadBuffers.push(std::move(buffer));
            while (!mReadBuffers.empty()) {
                wslay_event_recv(mCtx);
            }
        });
    mHttp->onStateChanged().connect([this](HttpClient::State state) {
            mState = static_cast<WebSocketClient::State>(state);
            mStateChanged.emit(static_cast<WebSocketClient::State>(state));
            if (state == HttpClient::Closed || state == HttpClient::Error) {
                if (mCtx) {
                    wslay_event_context_free(mCtx);
                    mCtx = nullptr;
                }
                mHttp.reset();
            }
        });
    mHttp->connect(host, port);
    // make an upgrade request
    HttpClient::Headers headers;
    headers.add("Host", host);
    headers.add("Upgrade", "websocket");
    headers.add("Connection", "upgrade");
    headers.add("Sec-WebSocket-Key", reinterpret_cast<char*>(encodedClientKey));
    headers.add("Sec-WebSocket-Version", "13");
    mHttp->get(HttpClient::v11, query, headers);
}

void WebSocketClient::close()
{
    if (!mHttp)
        return;
    assert(mCtx != nullptr);
    mHttp->close();
    wslay_event_context_free(mCtx);
    mCtx = nullptr;
    mHttp.reset();
}

void WebSocketClient::write()
{
    if (!mHttp || !mCtx)
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
