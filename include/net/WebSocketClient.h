#ifndef WEBSOCKETCLIENT_H
#define WEBSOCKETCLIENT_H

#include <net/TcpSocket.h>
#include <event/Loop.h>
#include <util/Creatable.h>
#include <net/TcpSocket.h>
#include <buffer/Buffer.h>
#include <buffer/Pool.h>
#include <deque>
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
    void attemptUpgrade(const std::string& encodedClientKey);
    void mergeReadBuffers();

private:
    std::shared_ptr<TcpSocket> mTcp;
    event::Signal<std::shared_ptr<buffer::Buffer>&&> mMessage;
    event::Signal<> mComplete;
    event::Signal<std::string&&> mError;
    size_t mBufferOffset;
    std::deque<std::shared_ptr<buffer::Buffer> > mReadBuffers, mWriteBuffers;
    wslay_event_context_ptr mCtx;
    bool mUpgraded;
    std::weak_ptr<event::Loop> mLoop;
};

inline WebSocketClient::~WebSocketClient()
{
    close();
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
    mWriteBuffers.push_back(std::move(buffer));
    write();
}

inline void WebSocketClient::write(const std::shared_ptr<buffer::Buffer>& buffer)
{
    mWriteBuffers.push_back(buffer);
    write();
}

inline void WebSocketClient::write(const uint8_t* data, size_t bytes)
{
    std::shared_ptr<buffer::Buffer> buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    buf->assign(data, bytes);
    mWriteBuffers.push_back(std::move(buf));
    write();
}

inline void WebSocketClient::write(const char* data, size_t bytes)
{
    std::shared_ptr<buffer::Buffer> buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    buf->assign(reinterpret_cast<const uint8_t*>(data), bytes);
    mWriteBuffers.push_back(std::move(buf));
    write();
}

}} // namespace reckoning::net

#endif // WEBSOCKETCLIENT_H
