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
    enum State {
        Idle,
        Connected,
        Upgraded,
        Closed,
        Error
    };
    event::Signal<State>& onStateChanged();
    State state() const;

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
    event::Signal<State> mStateChanged;
    State mState;
    size_t mBufferOffset;
    std::queue<std::shared_ptr<buffer::Buffer> > mReadBuffers, mWriteBuffers;
    wslay_event_context_ptr mCtx;
};

inline WebSocketClient::~WebSocketClient()
{
}

inline event::Signal<std::shared_ptr<buffer::Buffer>&&>& WebSocketClient::onMessage()
{
    return mMessage;
}

inline event::Signal<WebSocketClient::State>& WebSocketClient::onStateChanged()
{
    return mStateChanged;
}

inline WebSocketClient::State WebSocketClient::state() const
{
    return mState;
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
