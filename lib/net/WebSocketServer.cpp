#include "WebSocketServer.h"
#include <buffer/Pool.h>
#include <log/Log.h>
#include <util/Random.h>
#include <util/Base64.h>
#include <openssl/sha.h>
#include <wslay/wslay.h>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;

void WebSocketServer::close()
{
    if (!mServer)
        return;
    mServer.reset();
    mOnHttpRequest.disconnect();
    mOnHttpError.disconnect();
}

void WebSocketServer::setup()
{
    if (!mServer)
        return;
    mOnHttpRequest = mServer->onRequest().connect([this](std::shared_ptr<HttpServer::Request>&& request) {
            // is this an upgrade request?
            auto conn = request->headers.find("connection");
            if (!conn.empty())
                std::transform(conn.begin(), conn.end(), conn.begin(), ::tolower);
            auto upg = request->headers.find("upgrade");
            if (!upg.empty())
                std::transform(upg.begin(), upg.end(), upg.begin(), ::tolower);
            const auto sec = request->headers.find("sec-websocket-key");
            const auto ver = request->headers.find("sec-websocket-version");
            Log(Log::Error) << conn << upg << sec;
            if (conn != "upgrade" || upg != "websocket" || sec.empty() || ver.empty()) {
                // no
                return;
            }

            if (ver != "13") {
                HttpServer::Response response;
                response.status = 426;
                response.headers.add("Sec-WebSocket-Version", "13");
                request->write(std::move(response));
                return;
            }

            const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            uint8_t digest[SHA_DIGEST_LENGTH];
            SHA_CTX sha1ctx;
            SHA1_Init(&sha1ctx);
            SHA1_Update(&sha1ctx, &sec[0], sec.size());
            SHA1_Update(&sha1ctx, magic, 36);
            SHA1_Final(digest, &sha1ctx);
            uint8_t b64digest[29];
            size_t b64length = util::base64::encode(digest, sizeof(digest), b64digest, sizeof(b64digest));
            assert(b64length > sizeof(digest) && b64length < sizeof(b64digest));
            b64digest[b64length] = '\0';

            // send response back
            HttpServer::Response response;
            response.status = 101;
            response.headers.add("Upgrade", "websocket");
            response.headers.add("Connection", "Upgrade");
            response.headers.add("Sec-WebSocket-Accept", reinterpret_cast<char*>(b64digest));
            request->write(std::move(response));

            // and we're now ready to make our ws connection object
            auto connection = Connection::create(std::move(request->mSocket));
            request->neuter();
            connection->setup();

            mConnection.emit(std::move(connection));
        });
    mOnHttpError = mServer->onError().connect([this]() {
            // error?
            close();
        });
}

void WebSocketServer::Connection::close()
{
    mSocket.reset();
}

void WebSocketServer::Connection::setup()
{
    if (!mSocket)
        return;
    assert(mSocket->state() == TcpSocket::Connected);

    auto recvCallback = [](wslay_event_context_ptr ctx, uint8_t* data, size_t len, int flags, void* userData) -> ssize_t {
        Connection* ws = static_cast<Connection*>(userData);
        if (!ws->mSocket) {
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
        Connection* ws = static_cast<Connection*>(userData);
        if (!ws->mSocket) {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
            return 0;
        }
        ws->mSocket->write(data, len);
        return len;
    };

    auto msgrcvCallback = [](wslay_event_context_ptr ctx, const struct wslay_event_on_msg_recv_arg* arg, void* userData) {
        if (wslay_is_ctrl_frame(arg->opcode)) {
            // we don't deal with control frames yet
            return;
        }
        Connection* ws = static_cast<Connection*>(userData);
        if (!ws->mSocket) {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
            return;
        }
        auto buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get(arg->msg_length);
        buf->assign(arg->msg, arg->msg_length);
        ws->mMessage.emit(std::move(buf));
    };

    struct wslay_event_callbacks callbacks = {
        recvCallback,
        sendCallback,
        nullptr, /* genmask_callback */
        nullptr, /* on_frame_recv_start_callback */
        nullptr, /* on_frame_recv_callback */
        nullptr, /* on_frame_recv_end_callback */
        msgrcvCallback
    };

    wslay_event_context_server_init(&mCtx, &callbacks, this);

    mSocket->onData().connect([this](std::shared_ptr<buffer::Buffer>&& buffer) {
            if (mState != Connected || !wslay_event_want_read(mCtx))
                return;
            //mData.emit(std::move(buffer));
            assert(buffer);
            mReadBuffers.push(std::move(buffer));
            while (!mReadBuffers.empty()) {
                wslay_event_recv(mCtx);
            }
        });
    mSocket->onStateChanged().connect([this](TcpSocket::State state) {
            switch (state) {
            case TcpSocket::Closed:
                mState = Closed;
                mStateChanged.emit(Closed);
                mSocket.reset();
                break;
            case TcpSocket::Error:
                mState = Error;
                mStateChanged.emit(Error);
                mSocket.reset();
                break;
            default:
                break;
            }
        });
}

void WebSocketServer::Connection::write()
{
    if (!mSocket || !mCtx)
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
