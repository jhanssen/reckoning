#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <net/TcpServer.h>
#include <net/HttpClient.h>
#include <event/Signal.h>
#include <buffer/Buffer.h>
#include <buffer/Wait.h>
#include <map>

namespace reckoning {
namespace net {

class HttpServer : public std::enable_shared_from_this<HttpServer>, public util::Creatable<HttpServer>
{
public:
    ~HttpServer();

    using Headers = HttpClient::Headers;
    using HttpVersion = HttpClient::HttpVersion;
    using Response = HttpClient::Response;

    enum Method
    {
        Get,
        Post
    };

    struct Request : public util::Creatable<Request>
    {
    public:
        HttpVersion version;
        Method method;
        std::string query;
        Headers headers;

        void write(Response&& response);
        void write(std::shared_ptr<buffer::Buffer>&& data);
        void write(const std::shared_ptr<buffer::Buffer>& data);
        void write(const std::string& data);
        void write(const char* data, size_t size);

        void close();

        event::Signal<std::shared_ptr<buffer::Buffer>&&>& onBody();
        event::Signal<>& onEnd();
        event::Signal<>& onError();

    protected:
        Request();

        void finalize();

    private:
        std::shared_ptr<TcpSocket> mSocket;
        event::Signal<std::shared_ptr<buffer::Buffer>&&> mBody;
        event::Signal<> mEnd, mError;
        size_t mContentLength, mContentReceived;

        friend class HttpServer;
    };

    bool listen(uint16_t port);
    bool listen(const IPv4& ip, uint16_t port);
    bool listen(const IPv6& ip, uint16_t port);

    bool isListening() const;

    void close();

    event::Signal<std::shared_ptr<Request>&&>& onRequest();
    event::Signal<>& onError();

protected:
    HttpServer();

private:
    void setupServer();

private:
    struct Connection
    {
        Connection()
            : waitFor("\r\n\r\n")
        {
        }
        Connection(Connection&& other)
            : waitFor(std::move(other.waitFor))
        {
        }

        Connection& operator=(Connection&& other)
        {
            waitFor = std::move(other.waitFor);
            return *this;
        }

        buffer::Wait<5, 65536> waitFor;

    private:
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
    };

    std::map<std::shared_ptr<TcpSocket>, Connection> mConnections;
    std::shared_ptr<TcpServer> mServer;
    event::Signal<std::shared_ptr<Request>&&> mRequest;
    event::Signal<> mError;
};

inline HttpServer::HttpServer()
{
}

inline HttpServer::~HttpServer()
{
}

inline bool HttpServer::isListening() const
{
    return mServer && mServer->isListening();
}

inline event::Signal<std::shared_ptr<HttpServer::Request>&&>& HttpServer::onRequest()
{
    return mRequest;
}

inline event::Signal<>& HttpServer::onError()
{
    return mError;
}

inline HttpServer::Request::Request()
    : mContentLength(0), mContentReceived(0)
{
}

inline void HttpServer::Request::close()
{
    if (!mSocket)
        return;
    mSocket.reset();
}

inline void HttpServer::Request::write(std::shared_ptr<buffer::Buffer>&& data)
{
    if (!mSocket)
        return;
    mSocket->write(std::forward<std::shared_ptr<buffer::Buffer> >(data));
}

inline void HttpServer::Request::write(const std::shared_ptr<buffer::Buffer>& data)
{
    if (!mSocket)
        return;
    mSocket->write(data);
}

inline void HttpServer::Request::write(const std::string& data)
{
    if (!mSocket)
        return;
    mSocket->write(&data[0], data.size());
}

inline void HttpServer::Request::write(const char* data, size_t size)
{
    if (!mSocket)
        return;
    mSocket->write(data, size);
}

inline event::Signal<std::shared_ptr<buffer::Buffer>&&>& HttpServer::Request::onBody()
{
    return mBody;
}

inline event::Signal<>& HttpServer::Request::onEnd()
{
    return mEnd;
}

inline event::Signal<>& HttpServer::Request::onError()
{
    return mError;
}

}} // namespace reckoning::net

#endif // WebServer
