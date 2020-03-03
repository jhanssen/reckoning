#ifndef WEBSOCKETCLIENT_H
#define WEBSOCKETCLIENT_H

#include <net/HttpClient.h>
#include <util/Creatable.h>
#include <buffer/Buffer.h>
#include <buffer/Pool.h>
#include <queue>
#include <memory>

struct wslay_event_context;
typedef struct wslay_event_context *wslay_event_context_ptr;

namespace reckoning {
namespace net {

class WebSocketClient : public std::enable_shared_from_this<WebSocketClient>, public util::Creatable<WebSocketClient>
{
public:
    ~WebSocketClient();

    void close();

    event::Signal<std::shared_ptr<buffer::Buffer>&&>& onMessage();
    event::Signal<>& onComplete();
    event::Signal<std::string&&>& onError();

    void write(std::shared_ptr<buffer::Buffer>&& buffer);
    void write(const std::shared_ptr<buffer::Buffer>& buffer);
    void write(const uint8_t* data, size_t bytes);
    void write(const char* data, size_t bytes);

protected:
    WebSocketClient(const std::string& url);

private:
    void write();

private:
    std::shared_ptr<HttpClient> mHttp;
    event::Signal<std::shared_ptr<buffer::Buffer>&&> mMessage;
    event::Signal<> mComplete;
    event::Signal<std::string&&> mError;
    size_t mBufferOffset;
    std::queue<std::shared_ptr<buffer::Buffer> > mReadBuffers, mWriteBuffers, mFdWriteBuffers;
    wslay_event_context_ptr mCtx;
    bool mUpgraded;
    int mDupped;
    size_t mFdWriteOffset;
};

inline WebSocketClient::~WebSocketClient()
{
}

inline event::Signal<std::shared_ptr<buffer::Buffer>&&>& WebSocketClient::onMessage()
{
    return mMessage;
}

inline event::Signal<>& WebSocketClient::onComplete()
{
    return mComplete;
}

inline event::Signal<std::string&&>& WebSocketClient::onError()
{
    return mError;
}

inline void WebSocketClient::write(std::shared_ptr<buffer::Buffer>&& buffer)
{
    mWriteBuffers.push(std::move(buffer));
    write();
}

inline void WebSocketClient::write(const std::shared_ptr<buffer::Buffer>& buffer)
{
    mWriteBuffers.push(buffer);
    write();
}

inline void WebSocketClient::write(const uint8_t* data, size_t bytes)
{
    std::shared_ptr<buffer::Buffer> buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    buf->assign(data, bytes);
    mWriteBuffers.push(std::move(buf));
    write();
}

inline void WebSocketClient::write(const char* data, size_t bytes)
{
    std::shared_ptr<buffer::Buffer> buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    buf->assign(reinterpret_cast<const uint8_t*>(data), bytes);
    mWriteBuffers.push(std::move(buf));
    write();
}

}} // namespace reckoning::net

#endif // WEBSOCKETCLIENT_H
