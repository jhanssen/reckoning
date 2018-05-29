#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <event/EventLoop.h>
#include <event/Signal.h>
#include <net/Resolver.h>
#include <net/IPAddress.h>
#include <net/TcpSocket.h>
#include <buffer/Buffer.h>
#include <util/Creatable.h>
#include <memory>
#include <string>

namespace reckoning {
namespace net {

class TcpServer : public std::enable_shared_from_this<TcpServer>, public util::Creatable<TcpServer>
{
public:
    ~TcpServer();

    bool listen(uint16_t port);
    bool listen(const IPv4& ip, uint16_t port);
    bool listen(const IPv6& ip, uint16_t port);

    bool isListening() const;

    void close();

    event::Signal<std::shared_ptr<TcpSocket>&&>& onConnection();
    event::Signal<>& onError();

protected:
    TcpServer();

private:
    void socketCallback(int fd, uint8_t flags);

private:
    int mFd;
    event::EventLoop::FD mFdHandle;
    event::Signal<std::shared_ptr<TcpSocket>&&> mConnection;
    event::Signal<> mError;
    bool mIsIPv6;
};

inline TcpServer::TcpServer()
    : mFd(-1), mIsIPv6(false)
{
}

inline TcpServer::~TcpServer()
{
    close();
}

inline bool TcpServer::isListening() const
{
    return mFd != -1;
}

event::Signal<std::shared_ptr<TcpSocket>&&>& TcpServer::onConnection()
{
    return mConnection;
}

event::Signal<>& TcpServer::onError()
{
    return mError;
}

}} // namespace reckoning::net

#endif // TCPSERVER_H
