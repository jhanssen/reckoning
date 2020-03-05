#include <net/TcpSocket.h>
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

    auto process = [&](int& fd, event::Loop::FD& handle, int& otherfd, event::Loop::FD& otherHandle) {
        int e;
        if (flags & event::Loop::FdError) {
            // badness
            handle.remove();
            fd = -1;
            return;
        }
        if (flags & event::Loop::FdRead) {
            auto buf = read();
            if (buf) {
                mData.emit(std::move(buf));
            }
        }
        if (flags & event::Loop::FdWrite) {
            // remove select for write
            event::Loop::loop()->updateFd(fd, event::Loop::FdRead);

            if (mState & Connecting) {
                // check connect status
                int err;
                socklen_t size = sizeof(err);
                e = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &size);
                if (e == -1 || err != 0) {
                    // failed to connect
                    if (otherfd == -1) {
                        // we're done
                        mState = Error;
                        mStateChanged.emit(Error);
                    }
                    handle.remove();
                    fd = -1;
                    return;
                } else {
                    // we're good, if the other socket is still open, close it
                    mState = Connected;
                    mStateChanged.emit(Connected);
                    if (otherfd != -1) {
                        otherHandle.remove();
                        otherfd = -1;
                    }
                }
            }

            processWrite(fd);
        }
    };

    if (fd == mFd4) {
        // ipv4
        process(mFd4, mFd4Handle, mFd6, mFd6Handle);
    } else if (fd == mFd6) {
        process(mFd6, mFd6Handle, mFd4, mFd4Handle);
    }
}

void TcpSocket::connect(const std::string& host, uint16_t port)
{
    std::weak_ptr<TcpSocket> weak = shared_from_this();

    mResolver = std::make_shared<Resolver::Response>(host);
    mResolver->onIPv4().connect([weak, port](IPv4&& ip) {
            //Log(Log::Info) << "resolved to" << ip.name();
            if (auto socket = weak.lock()) {
                socket->connect(ip, port);
            }
        });
    mResolver->onIPv6().connect([weak, port](IPv6&& ip) {
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

void TcpSocket::connect(const IPv4& ip, uint16_t port)
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
    mFd4Handle = event::Loop::loop()->addFd(mFd4, event::Loop::FdRead|event::Loop::FdWrite,
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
            mFd6Handle.remove();
            mFd6 = -1;
        }
        processWrite(mFd4);
    } else if (errno == EINPROGRESS) {
        // we're pending connect
        if (mState < Connecting) {
            mState = Connecting;
            mStateChanged.emit(Connecting);
        }
        // printf("connecting %d\n", mFd4);
    } else {
        // bad stuff
        if (mFd6 == -1) {
            mState = Error;
            mStateChanged.emit(Error);
        }
        mFd4Handle.remove();
        mFd4 = -1;
    }
}

void TcpSocket::connect(const IPv6& ip, uint16_t port)
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
    mFd6Handle = event::Loop::loop()->addFd(mFd6, event::Loop::FdRead|event::Loop::FdWrite,
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
            mFd4Handle.remove();
            mFd4 = -1;
        }
        processWrite(mFd6);
    } else if (errno == EINPROGRESS) {
        // we're pending connect
        if (mState < Connecting) {
            mState = Connecting;
            mStateChanged.emit(Connecting);
        }
        // printf("connecting %d\n", mFd6);
    } else {
        // bad stuff
        if (mFd4 == -1) {
            mState = Error;
            mStateChanged.emit(Error);
        }
        mFd6Handle.remove();
        mFd6 = -1;
    }
}

void TcpSocket::setSocket(int fd, bool ipv6)
{
    if (mFd6 != -1) {
        Log(Log::Error) << "socket set on connected ipv6 socket";
        return;
    }
    if (mFd4 != -1) {
        Log(Log::Error) << "socket set on connected ipv4 socket";
        return;
    }
    auto& fdes = (ipv6 ? mFd6 : mFd4);
    auto& handle = (ipv6 ? mFd6Handle : mFd4Handle);
    fdes = fd;
    mState = Connected;
    handle = event::Loop::loop()->addFd(fdes, event::Loop::FdRead,
                                        std::bind(&TcpSocket::socketCallback, this, std::placeholders::_1, std::placeholders::_2));
}

void TcpSocket::close()
{
    if (mFd4 != -1) {
        mFd4Handle.remove();
        mFd4 = -1;
    }
    if (mFd6 != -1) {
        mFd6Handle.remove();
        mFd6 = -1;
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
                event::Loop::loop()->updateFd(fd, event::Loop::FdRead|event::Loop::FdWrite);
            } else {
                Log(Log::Error) << "failed to write to fd" << fd << errno;

                close();

                mState = Error;
                mStateChanged.emit(Error);
            }
            return;
        } else {
            if (static_cast<size_t>(e) < (*it)->size() - mWriteOffset) {
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

        buf->assign(data + where, cur);

        write(std::move(buf));

        rem -= cur;
        where += cur;
    }
}
