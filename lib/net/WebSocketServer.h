#ifndef WEBSOCKETSERVER_H
#define WEBSOCKETSERVER_H

#include <net/HttpServer.h>
#include <util/Creatable.h>
#include <event/Signal.h>
#include <buffer/Buffer.h>
#include <queue>

struct wslay_event_context;
typedef struct wslay_event_context *wslay_event_context_ptr;

namespace reckoning {
namespace net {

class WebSocketServer : public std::enable_shared_from_this<WebSocketServer>, public util::Creatable<WebSocketServer>
{
public:
    ~WebSocketServer();

    class Connection : public util::Creatable<Connection>
    {
    public:
        ~Connection();

        void write(std::shared_ptr<buffer::Buffer>&& buffer);
        void write(const std::shared_ptr<buffer::Buffer>& buffer);
        void write(const uint8_t* data, size_t bytes);
        void write(const char* data, size_t bytes);

        void close();

        event::Signal<std::shared_ptr<buffer::Buffer>&&>& onMessage();
        enum State {
            Connected,
            Closed,
            Error
        };
        event::Signal<State>& onStateChanged();

    protected:
        Connection(const std::shared_ptr<TcpSocket>& socket);
        Connection(std::shared_ptr<TcpSocket>&& socket);

    private:
        void setup();
        void write();

    private:
        std::shared_ptr<TcpSocket> mSocket;
        event::Signal<std::shared_ptr<buffer::Buffer>&&> mMessage;
        event::Signal<State> mStateChanged;
        State mState;
        wslay_event_context_ptr mCtx;
        std::queue<std::shared_ptr<buffer::Buffer> > mReadBuffers, mWriteBuffers;
        size_t mBufferOffset;

        friend class WebSocketServer;
    };

    event::Signal<std::shared_ptr<Connection>&&>& onConnection();
    event::Signal<>& onError();

    void close();

protected:
    WebSocketServer(const std::shared_ptr<HttpServer>& server);
    WebSocketServer(std::shared_ptr<HttpServer>&& server);

private:
    void setup();

private:
    std::shared_ptr<HttpServer> mServer;
    event::Signal<std::shared_ptr<Connection>&&> mConnection;
    event::Signal<> mError;

    event::Signal<std::shared_ptr<HttpServer::Request>&&>::Connection mOnHttpRequest;
    event::Signal<>::Connection mOnHttpError;
};

inline WebSocketServer::WebSocketServer(const std::shared_ptr<HttpServer>& server)
    : mServer(server)
{
    setup();
}

inline WebSocketServer::WebSocketServer(std::shared_ptr<HttpServer>&& server)
    : mServer(std::forward<std::shared_ptr<HttpServer> >(server))
{
    setup();
}

inline WebSocketServer::~WebSocketServer()
{
}

inline event::Signal<std::shared_ptr<WebSocketServer::Connection>&&>& WebSocketServer::onConnection()
{
    return mConnection;
}

inline event::Signal<>& WebSocketServer::onError()
{
    return mError;
}

inline WebSocketServer::Connection::Connection(const std::shared_ptr<TcpSocket>& socket)
    : mSocket(socket), mState(Connected), mBufferOffset(0)
{
}

inline WebSocketServer::Connection::Connection(std::shared_ptr<TcpSocket>&& socket)
    : mSocket(std::forward<std::shared_ptr<TcpSocket> >(socket)), mState(Connected), mBufferOffset(0)
{
}

inline WebSocketServer::Connection::~Connection()
{
    close();
}

inline void WebSocketServer::Connection::write(std::shared_ptr<buffer::Buffer>&& buffer)
{
    mWriteBuffers.push(std::forward<std::shared_ptr<buffer::Buffer> >(buffer));
    write();
}

inline void WebSocketServer::Connection::write(const std::shared_ptr<buffer::Buffer>& buffer)
{
    mWriteBuffers.push(buffer);
    write();
}

inline void WebSocketServer::Connection::write(const uint8_t* data, size_t bytes)
{
    std::shared_ptr<buffer::Buffer> buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    buf->assign(data, bytes);
    mWriteBuffers.push(std::move(buf));
    write();
}

inline void WebSocketServer::Connection::write(const char* data, size_t bytes)
{
    std::shared_ptr<buffer::Buffer> buf = buffer::Pool<20, TcpSocket::BufferSize>::pool().get();
    buf->assign(reinterpret_cast<const uint8_t*>(data), bytes);
    mWriteBuffers.push(std::move(buf));
    write();
}

event::Signal<std::shared_ptr<buffer::Buffer>&&>& WebSocketServer::Connection::onMessage()
{
    return mMessage;
}

event::Signal<WebSocketServer::Connection::State>& WebSocketServer::Connection::onStateChanged()
{
    return mStateChanged;
}

}} // namespace reckoning::net

#endif // WEBSOCKETSERVER_H
