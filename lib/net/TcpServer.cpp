#include "TcpServer.h"
#include "config.h"
#include <util/Socket.h>
#include <log/Log.h>
#include <fcntl.h>
#include <unistd.h>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;

static inline int listenHelper(struct sockaddr* address, socklen_t len, int backlog)
{
    int fd = socket((len == sizeof(sockaddr_in) ? AF_INET : AF_INET6), SOCK_STREAM, 0);
    if (fd == -1)
        return -1;
#ifdef HAVE_NONBLOCK
    util::socket::setFlag(fd, O_NONBLOCK);
#endif
    int flags = 1, e;
#ifdef HAVE_NOSIGPIPE
    e = ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (void *)&flags, sizeof(int));
    if (e == -1) {
        close(fd);
        return -1;
    }
#endif
    // nodelay
    flags = 1;
    e = ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&flags, sizeof(int));
    if (e == -1) {
        close(fd);
        return -1;
    }
    e = bind(fd, address, len);
    if (e == -1) {
        close(fd);
        return -1;
    }
    e = listen(fd, backlog);
    if (e == -1) {
        close(fd);
        return -1;
    }
    return fd;
}

void TcpServer::socketCallback(int fd, uint8_t flags)
{
    union {
        sockaddr_in client4;
        sockaddr_in6 client6;
        sockaddr client;
    };
    socklen_t size = 0;
    if (mIsIPv6) {
        size = sizeof(client6);
    } else {
        size = sizeof(client4);
    }
    int e;
    for (;;) {
        eintrwrap(e, ::accept(fd, &client, &size));
        if (e == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            // bad
            Log(Log::Error) << "TcpServer failed to accept" << errno;
            close();
            mError.emit();
            return;
        }
        // we have our accepted client
        auto socket = TcpSocket::create();
        socket->setSocket(e, mIsIPv6);
        mConnection.emit(std::move(socket));
    }
}

bool TcpServer::listen(uint16_t port)
{
    sockaddr_in addr;
    memset(&addr, '\0', sizeof(sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    mFd = listenHelper(reinterpret_cast<sockaddr*>(&addr), sizeof(addr), 10);
    if (mFd == -1) {
        Log(Log::Error) << "TcpServer failed to listen";
        return false;
    }
    mIsIPv6 = false;
    mFdHandle = event::Loop::loop()->addFd(mFd, event::Loop::FdRead,
                                           std::bind(&TcpServer::socketCallback, this, std::placeholders::_1, std::placeholders::_2));
    return true;
}

bool TcpServer::listen(const IPv4& ip, uint16_t port)
{
    sockaddr_in addr;
    memset(&addr, '\0', sizeof(sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr = ip.ip();
    addr.sin_port = htons(port);
    mFd = listenHelper(reinterpret_cast<sockaddr*>(&addr), sizeof(addr), 10);
    if (mFd == -1) {
        Log(Log::Error) << "TcpServer failed to listen";
        return false;
    }
    mIsIPv6 = false;
    mFdHandle = event::Loop::loop()->addFd(mFd, event::Loop::FdRead,
                                           std::bind(&TcpServer::socketCallback, this, std::placeholders::_1, std::placeholders::_2));
    return true;
}

bool TcpServer::listen(const IPv6& ip, uint16_t port)
{
    sockaddr_in6 addr;
    memset(&addr, '\0', sizeof(sockaddr_in));
    addr.sin6_family = AF_INET;
    addr.sin6_addr = ip.ip();
    addr.sin6_port = htons(port);
    mFd = listenHelper(reinterpret_cast<sockaddr*>(&addr), sizeof(addr), 10);
    if (mFd == -1) {
        Log(Log::Error) << "TcpServer failed to listen";
        return false;
    }
    mIsIPv6 = true;
    mFdHandle = event::Loop::loop()->addFd(mFd, event::Loop::FdRead,
                                           std::bind(&TcpServer::socketCallback, this, std::placeholders::_1, std::placeholders::_2));
    return true;
}

void TcpServer::close()
{
    if (mFd == -1)
        return;
    mFdHandle.remove();
    mFd = -1;
}
