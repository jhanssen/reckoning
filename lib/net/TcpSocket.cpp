#include "TcpSocket.h"
#include <log/Log.h>
#include <util/Socket.h>
#include <buffer/Pool.h>
#include <fcntl.h>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;

TcpSocket::TcpSocket()
    : mFd4(-1), mFd6(-1), mWriteOffset(0), mState(Idle)
{
}

TcpSocket::~TcpSocket()
{
    close();
}

void TcpSocket::socketCallback(int fd, uint8_t flags)
{
    assert(fd != -1);

    int e;
    if (fd == mFd4) {
        // ipv4
        if (flags & event::EventLoop::FdError) {
            // badness
            eintrwrap(e, ::close(mFd4));
            mFd4 = -1;
            mFd4Handle.remove();
            return;
        }
        if (flags & event::EventLoop::FdRead) {
            auto buf = read();
            if (buf)
                mData.emit(std::move(buf));
        }
        if (flags & event::EventLoop::FdWrite) {
            // remove select for write
            event::EventLoop::loop()->fd(fd, event::EventLoop::FdRead);

            if (mState & Connecting) {
                // check connect status
                int err;
                socklen_t size = sizeof(err);
                int e = ::getsockopt(mFd4, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &size);
                if (e == -1 || err != 0) {
                    // failed to connect
                    if (mFd6 == -1) {
                        // we're done
                        mState = Error;
                        mStateChanged.emit(Error);
                    }
                    eintrwrap(e, ::close(mFd4));
                    mFd4 = -1;
                    mFd4Handle.remove();
                    return;
                } else {
                    // we're good, if IPv6 is still connected, close it
                    mState = Connected;
                    mStateChanged.emit(Connected);
                    if (mFd6 != -1) {
                        eintrwrap(e, ::close(mFd6));
                        mFd6 = -1;
                        mFd6Handle.remove();
                    }
                    processWrite(mFd4);
                }
            }

            processWrite(mFd6);
        }
    } else if (fd == mFd6) {
        // ipv6
        if (flags & event::EventLoop::FdError) {
            // badness
            eintrwrap(e, ::close(mFd6));
            mFd6 = -1;
            mFd6Handle.remove();
            return;
        }
        if (flags & event::EventLoop::FdRead) {
            auto buf = read();
            if (buf)
                mData.emit(std::move(buf));
        }
        if (flags & event::EventLoop::FdWrite) {
            // remove select for write
            event::EventLoop::loop()->fd(fd, event::EventLoop::FdRead);

            if (mState & Connecting) {
                // check connect status
                int err;
                socklen_t size = sizeof(err);
                int e = ::getsockopt(mFd6, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &size);
                if (e == -1 || err != 0) {
                    // failed to connect
                    if (mFd4 == -1) {
                        // we're done
                        mState = Error;
                        mStateChanged.emit(Error);
                    }
                    eintrwrap(e, ::close(mFd6));
                    mFd6 = -1;
                    mFd6Handle.remove();
                    return;
                } else {
                    // we're good, if IPv4 is still connected, close it
                    mState = Connected;
                    mStateChanged.emit(Connected);
                    if (mFd4 != -1) {
                        eintrwrap(e, ::close(mFd4));
                        mFd4 = -1;
                        mFd4Handle.remove();
                    }
                    processWrite(mFd6);
                }
            }

            processWrite(mFd6);
        }
    }
}

void TcpSocket::connect(const std::string& host, uint16_t port)
{
    std::weak_ptr<TcpSocket> weak = shared_from_this();

    mResolver = std::make_shared<Resolver::Response>(host);
    mResolver->onIPv4().connect([weak, port](Resolver::Response::IPv4&& ip) {
            //Log(Log::Info) << "resolved to" << ip.name();
            if (auto socket = weak.lock()) {
                socket->connect(ip, port);
            }
        });
    mResolver->onIPv6().connect([weak, port](Resolver::Response::IPv6&& ip) {
            // Log(Log::Info) << "resolved to" << ip.name();
            if (auto socket = weak.lock()) {
                socket->connect(ip, port);
            }
        });
    mResolver->onError().connect([weak](std::string&& msg) {
            if (auto socket = weak.lock()) {
                socket->mResolver.reset();
            }
        });
    mResolver->onComplete().connect([weak]() {
            if (auto socket = weak.lock()) {
                socket->mResolver.reset();
            }
        });
    Resolver::resolver().startRequest(mResolver);
}

void TcpSocket::connect(const Resolver::Response::IPv4& ip, uint16_t port)
{
    if (mFd4 != -1 || mState == Connected) {
        return;
    }

    std::weak_ptr<TcpSocket> weak = shared_from_this();
    mFd4 = socket(AF_INET, SOCK_STREAM, 0);
    int e = 1;
#ifdef HAVE_NOSIGPIPE
    ::setsockopt(mFd4, SOL_SOCKET, SO_NOSIGPIPE, (void *)&e, sizeof(int));
#endif
#ifdef HAVE_NONBLOCK
    util::socket::setFlag(mFd4, O_NONBLOCK);
#endif
    mFd4Handle = event::EventLoop::loop()->fd(mFd4, event::EventLoop::FdRead|event::EventLoop::FdWrite,
                                              std::bind(&TcpSocket::socketCallback, this, std::placeholders::_1, std::placeholders::_2));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = ip.ip();
    eintrwrap(e, ::connect(mFd4, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_in)));
    if (!e) {
        // we're connected
        mState = Connected;
        mStateChanged.emit(Connected);
        // if we have a pending IPv6 connect, close it
        if (mFd6 != -1) {
            eintrwrap(e, ::close(mFd6));
            mFd6 = -1;
            mFd6Handle.remove();
        }
        processWrite(mFd4);
    } else if (errno == EINPROGRESS) {
        // we're pending connect
        mState = Connecting;
        mStateChanged.emit(Connecting);
        // printf("connecting %d\n", mFd4);
    } else {
        // bad stuff
        if (mFd6 == -1) {
            mState = Error;
            mStateChanged.emit(Error);
        }
        eintrwrap(e, ::close(mFd4));
        mFd4 = -1;
        mFd4Handle.remove();
    }
}

void TcpSocket::connect(const Resolver::Response::IPv6& ip, uint16_t port)
{
    if (mFd6 != -1 || mState == Connected) {
        return;
    }
    std::weak_ptr<TcpSocket> weak = shared_from_this();
    mFd6 = socket(AF_INET6, SOCK_STREAM, 0);
    int e = 1;
#ifdef HAVE_NOSIGPIPE
    ::setsockopt(mFd6, SOL_SOCKET, SO_NOSIGPIPE, (void *)&e, sizeof(int));
#endif
#ifdef HAVE_NONBLOCK
    util::socket::setFlag(mFd6, O_NONBLOCK);
#endif
    mFd6Handle = event::EventLoop::loop()->fd(mFd6, event::EventLoop::FdRead|event::EventLoop::FdWrite,
                                              std::bind(&TcpSocket::socketCallback, this, std::placeholders::_1, std::placeholders::_2));
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(sockaddr_in6));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = ip.ip();
    eintrwrap(e, ::connect(mFd6, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_in6)));
    if (!e) {
        // we're connected
        mState = Connected;
        mStateChanged.emit(Connected);
        // if we have a pending IPv4 connect, close it
        if (mFd4 != -1) {
            eintrwrap(e, ::close(mFd4));
            mFd4 = -1;
            mFd4Handle.remove();
        }
        processWrite(mFd6);
    } else if (errno == EINPROGRESS) {
        // we're pending connect
        mState = Connecting;
        mStateChanged.emit(Connecting);
        // printf("connecting %d\n", mFd6);
    } else {
        // bad stuff
        if (mFd4 == -1) {
            mState = Error;
            mStateChanged.emit(Error);
        }
        eintrwrap(e, ::close(mFd6));
        mFd6 = -1;
        mFd6Handle.remove();
    }
}

void TcpSocket::close()
{
    int e;
    if (mFd4 != -1) {
        eintrwrap(e, ::close(mFd4));
        mFd4 = -1;
        mFd4Handle.remove();
    }
    if (mFd6 != -1) {
        eintrwrap(e, ::close(mFd6));
        mFd6 = -1;
        mFd6Handle.remove();
    }
}

void TcpSocket::processWrite(int fd)
{
    int e;
    auto it = mPendingWrites.begin();
    const auto end = mPendingWrites.end();
    while (it != end) {
        // write as much as we can
        eintrwrap(e, ::write(fd, (*it)->data() + mWriteOffset, (*it)->size() - mWriteOffset));
        if (e == -1) {
            // welp
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // hey, good stuff. reenable the write flag
                event::EventLoop::loop()->fd(fd, event::EventLoop::FdRead|event::EventLoop::FdWrite);
            } else {
                Log(Log::Error) << "failed to write to fd" << fd << errno;

                close();

                mState = Error;
                mStateChanged.emit(Error);
            }
            return;
        } else {
            if (e < (*it)->size() - mWriteOffset) {
                mWriteOffset += e;
            } else {
                mWriteOffset = 0;
                ++it;
            }
        }
    }
    mPendingWrites.erase(mPendingWrites.begin(), it);
}

std::shared_ptr<buffer::Buffer> TcpSocket::read(size_t bytes)
{
    std::shared_ptr<buffer::Buffer> buf = buffer::Pool<20, BufferSize>::pool().get();
    assert(buf);

    auto processRead = [bytes](int fd, std::shared_ptr<buffer::Buffer>& buf) {
        int e;
        eintrwrap(e, ::read(fd, buf->data(), std::min<size_t>(bytes, BufferSize)));
        if (e > 0) {
            buf->setSize(e);
            return e;
        } else if (!e) {
            // disconnected
            return 0;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // done for now
            buf->setSize(0);
            return -EAGAIN;
        }
        // badness
        return -errno;
    };

    int e = -1;
    if (mFd4 != -1) {
        assert(mFd6 == -1);
        e = processRead(mFd4, buf);
    } else if (mFd6 != -1) {
        assert(mFd4 == -1);
        e = processRead(mFd6, buf);
    } else {
        assert(false && "No fd open for read?");
    }
    if (e > 0) {
        return buf;
    } else if (!e) {
        close();

        mState = Closed;
        mStateChanged.emit(Closed);

        return std::shared_ptr<buffer::Buffer>();
    } else if (errno == -EAGAIN) {
        return std::shared_ptr<buffer::Buffer>();
    }

    close();

    mState = Error;
    mStateChanged.emit(Error);

    Log(Log::Error) << "failed to read" << -e;
    return std::shared_ptr<buffer::Buffer>();
}

void TcpSocket::write(const uint8_t* data, size_t bytes)
{
    size_t rem = bytes;
    size_t where = 0;
    while (rem > 0) {
        std::shared_ptr<buffer::Buffer> buf = buffer::Pool<20, BufferSize>::pool().get();
        const size_t cur = std::min<size_t>(rem, BufferSize);

        memcpy(buf->data(), data + where, cur);
        buf->setSize(cur);

        write(std::move(buf));

        rem -= cur;
        where += cur;
    }
}
