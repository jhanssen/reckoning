#include <net/TcpSocket.h>
#include <log/Log.h>
#include <util/Socket.h>
#include <buffer/Pool.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <mutex>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;

std::string TcpSocket::sCAFile;
std::string TcpSocket::sCAPath;

static std::once_flag initTLSFlag;

TcpSocket::TcpSocket()
    : mMode(Plain), mFd4(-1), mFd6(-1), mWriteOffset(0), mState(Idle)
{
}

TcpSocket::~TcpSocket()
{
    close();
}

void TcpSocket::initTLS()
{
    std::call_once(initTLSFlag, [](){
        SSL_library_init();
        SSL_load_error_strings();
    });

    if (mSsl.ctx != nullptr)
        return;

    auto meth = TLS_client_method();
    mSsl.ctx = SSL_CTX_new(meth);
    SSL_CTX_set_mode(mSsl.ctx, SSL_MODE_RELEASE_BUFFERS);
    if (!sCAFile.empty() || !sCAPath.empty()) {
        SSL_CTX_load_verify_locations(mSsl.ctx,
                                      sCAFile.empty() ? nullptr : sCAFile.c_str(),
                                      sCAPath.empty() ? nullptr : sCAPath.c_str());
    } else {
        SSL_CTX_set_default_verify_paths(mSsl.ctx);
    }
    int ctx_options = SSL_OP_ALL;
    ctx_options |= SSL_OP_NO_SSLv2;
    ctx_options |= SSL_OP_NO_SSLv3;
    SSL_CTX_set_options(mSsl.ctx, ctx_options);
    SSL_CTX_set_verify(mSsl.ctx, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_verify_depth(mSsl.ctx, 2);
    mSsl.session = SSL_new(mSsl.ctx);
}

void TcpSocket::connectTLS(int fd)
{
    int e = SSL_connect(mSsl.session);
    if (e == 1) {
        // done
        mSsl.writeWaitState = SSLNotWaiting;

        mState = Connected;
        mStateChanged.emit(Connected);

        processWrite(fd);
        return;
    }
    int status = SSL_get_error(mSsl.session, e);
    switch (status) {
    case SSL_ERROR_WANT_WRITE:
        mSsl.writeWaitState = SSLWriteWaitingForWrite;
        event::Loop::loop()->updateFd(fd, event::Loop::FdRead | event::Loop::FdWrite);
        break;
    case SSL_ERROR_WANT_READ:
        mSsl.writeWaitState = SSLWriteWaitingForRead;
        break;
    case SSL_ERROR_ZERO_RETURN:
        // connection closed?
        mState = Closed;
        mStateChanged.emit(Closed);

        close();
        break;
    default:
        char msg[1024];
        ERR_error_string_n(ERR_get_error(), msg, sizeof(msg));

        Log(Log::Error) << "ssl error (connectTLS)" << e << status << msg;

        // badness
        mState = Error;
        mStateChanged.emit(Error);

        close();
        break;
    }
}

void TcpSocket::socketCallback(int fd, uint8_t flags)
{
    assert(fd != -1);

    auto processPlain = [&](int& fd, event::Loop::FD& handle, int& otherfd, event::Loop::FD& otherHandle) {
        int e;
        if (flags & event::Loop::FdError) {
            // badness
            handle.remove();
            fd = -1;
            return;
        }
        if (flags & event::Loop::FdRead) {
            for (;;) {
                auto buf = read();
                if (buf) {
                    mData.emit(std::move(buf));
                } else {
                    break;
                }
            }
        }
        if (flags & event::Loop::FdWrite) {
            // remove select for write
            event::Loop::loop()->updateFd(fd, event::Loop::FdRead);

            if (mState == Connecting) {
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
                    if (otherfd != -1) {
                        otherHandle.remove();
                        otherfd = -1;
                    }
                    mState = Connected;
                    mStateChanged.emit(Connected);
                }
            }

            processWrite(fd);
        }
    };

    auto processTLS = [&](int& fd, event::Loop::FD& handle, int& otherfd, event::Loop::FD& otherHandle) {
        int e;
        if (flags & event::Loop::FdError) {
            // badness
            handle.remove();
            fd = -1;
            return;
        }
        if (flags & event::Loop::FdRead) {
            switch (mState) {
            case Handshaking:
                if (mSsl.writeWaitState == SSLWriteWaitingForRead) {
                    connectTLS(fd);
                }
                break;
            case Connected:
                if (mSsl.readWaitState == SSLReadWaitingForRead) {
                    // retry read
                    auto buf = read();
                    if (buf) {
                        mData.emit(std::move(buf));
                    }
                }
                if (mSsl.writeWaitState == SSLWriteWaitingForRead) {
                    // retry write
                }
                while (mSsl.readWaitState == SSLNotWaiting) {
                    // do the reads
                    auto buf = read();
                    if (buf) {
                        mData.emit(std::move(buf));
                    } else {
                        break;
                    }
                }
                break;
            default:
                // shouldn't happen
                break;
            }
        }
        if (flags & event::Loop::FdWrite) {
            // remove select for write
            event::Loop::loop()->updateFd(fd, event::Loop::FdRead);

            switch (mState) {
            case Connecting: {
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
                    if (otherfd != -1) {
                        otherHandle.remove();
                        otherfd = -1;
                    }
                    mState = Handshaking;
                    mStateChanged.emit(Handshaking);

                    assert(mSsl.session != nullptr && mSsl.bio == nullptr);
                    mSsl.bio = BIO_new_socket(fd, BIO_NOCLOSE);
                    SSL_set_bio(mSsl.session, mSsl.bio, mSsl.bio);
                    SSL_set_connect_state(mSsl.session);
                    connectTLS(fd);
                }
                break; }
            case Handshaking:
                if (mSsl.writeWaitState == SSLWriteWaitingForWrite) {
                    connectTLS(fd);
                }
                if (mState == Connected) {
                    assert(mSsl.writeWaitState == SSLNotWaiting);
                    processWrite(fd);
                }
                break;
            case Connected:
                if (mSsl.readWaitState == SSLReadWaitingForWrite) {
                    // retry read
                    auto buf = read();
                    if (buf) {
                        mData.emit(std::move(buf));
                    }
                }
                if (mSsl.writeWaitState == SSLWriteWaitingForWrite || mSsl.writeWaitState == SSLNotWaiting) {
                    // do writes
                    processWrite(fd);
                }
                break;
            default:
                // shouldn't happen
                break;
            }
        }
    };

    if (fd == mFd4) {
        // ipv4
        if (mMode == Plain) {
            processPlain(mFd4, mFd4Handle, mFd6, mFd6Handle);
        } else {
            processTLS(mFd4, mFd4Handle, mFd6, mFd6Handle);
        }
    } else if (fd == mFd6) {
        if (mMode == Plain) {
            processPlain(mFd6, mFd6Handle, mFd4, mFd4Handle);
        } else {
            processTLS(mFd6, mFd6Handle, mFd4, mFd4Handle);
        }
    }
}

void TcpSocket::connect(const std::string& host, uint16_t port, Mode mode)
{
    std::weak_ptr<TcpSocket> weak = shared_from_this();

    mResolver = std::make_shared<Resolver::Response>(host);
    mResolver->onIPv4().connect([weak, port, mode](IPv4&& ip) {
            //Log(Log::Info) << "resolved to" << ip.name();
            if (auto socket = weak.lock()) {
                socket->connect(ip, port, mode);
            }
        });
    mResolver->onIPv6().connect([weak, port, mode](IPv6&& ip) {
            // Log(Log::Info) << "resolved to" << ip.name();
            if (auto socket = weak.lock()) {
                socket->connect(ip, port, mode);
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

void TcpSocket::connect(const IPv4& ip, uint16_t port, Mode mode)
{
    if (mFd4 != -1 || mState == Connected) {
        return;
    }

    mMode = mode;
    if (mMode == TLS) {
        initTLS();
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
        if (mMode == Plain) {
            mState = Connected;
            mStateChanged.emit(Connected);
            processWrite(mFd4);
        } else {
            mState = Handshaking;
            mStateChanged.emit(Handshaking);

            assert(mSsl.session != nullptr && mSsl.bio == nullptr);
            mSsl.bio = BIO_new_socket(mFd4, BIO_NOCLOSE);
            SSL_set_bio(mSsl.session, mSsl.bio, mSsl.bio);
            SSL_set_connect_state(mSsl.session);
            connectTLS(mFd4);
        }
        // if we have a pending IPv6 connect, close it
        if (mFd6 != -1) {
            mFd6Handle.remove();
            mFd6 = -1;
        }
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

void TcpSocket::connect(const IPv6& ip, uint16_t port, Mode mode)
{
    if (mFd6 != -1 || mState == Connected) {
        return;
    }

    mMode = mode;
    if (mMode == TLS) {
        initTLS();
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
        if (mMode == Plain) {
            mState = Connected;
            mStateChanged.emit(Connected);
            processWrite(mFd6);
        } else {
            mState = Handshaking;
            mStateChanged.emit(Handshaking);

            assert(mSsl.session != nullptr && mSsl.bio == nullptr);
            mSsl.bio = BIO_new_socket(mFd6, BIO_NOCLOSE);
            SSL_set_bio(mSsl.session, mSsl.bio, mSsl.bio);
            SSL_set_connect_state(mSsl.session);
            connectTLS(mFd6);
        }
        // if we have a pending IPv4 connect, close it
        if (mFd4 != -1) {
            mFd4Handle.remove();
            mFd4 = -1;
        }
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
    if (mMode == TLS) {
        SSL_shutdown(mSsl.session);
        SSL_free(mSsl.session);
        SSL_CTX_free(mSsl.ctx);
        mSsl.bio = nullptr;
        mSsl.session = nullptr;
        mSsl.ctx = nullptr;
        mMode = Plain;
    }
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
    auto writePlain = [&](const std::shared_ptr<buffer::Buffer>& buffer) {
        int e;
        eintrwrap(e, ::write(fd, buffer->data() + mWriteOffset, buffer->size() - mWriteOffset));
        if (e == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return -EAGAIN;
            return -errno;
        }
        return e;
    };

    auto writeTLS = [&](const std::shared_ptr<buffer::Buffer>& buffer) {
        int e = SSL_write(mSsl.session, buffer->data() + mWriteOffset, buffer->size() - mWriteOffset);
        if (e > 0)
            return e;
        e = SSL_get_error(mSsl.session, e);
        switch (e) {
        case SSL_ERROR_WANT_READ:
            mSsl.writeWaitState = SSLReadWaitingForRead;
            return -EAGAIN;
        case SSL_ERROR_WANT_WRITE:
            mSsl.writeWaitState = SSLReadWaitingForWrite;
            return -EAGAIN;
        }

        char msg[1024];
        ERR_error_string_n(ERR_get_error(), msg, sizeof(msg));
        Log(Log::Error) << "ssl error (writeTLS)" << e << msg;
        return -e;
    };

    int e;
    auto it = mPendingWrites.begin();
    const auto end = mPendingWrites.end();
    while (it != end) {
        // write as much as we can
        if (mMode == Plain) {
            e = writePlain(*it);
        } else {
            e = writeTLS(*it);
        }
        if (e < 0) {
            // welp
            if (e == -EAGAIN) {
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
    if (mFd4 == -1 && mFd6 == -1) {
        // sorry, we're closed
        return std::shared_ptr<buffer::Buffer>();
    }

    std::shared_ptr<buffer::Buffer> buf = buffer::Pool<20, BufferSize>::pool().get();
    assert(buf);

    auto processReadPlain = [](int fd, std::shared_ptr<buffer::Buffer>& buf, size_t bytes) -> int {
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

    auto processReadTLS = [](SSL* ssl, std::shared_ptr<buffer::Buffer>& buf, size_t bytes, SSLWaitState& state) -> int {
        int e = SSL_read(ssl, buf->data(), std::min<size_t>(bytes, BufferSize));
        if (e > 0) {
            buf->setSize(e);
            return e;
        }
        e = SSL_get_error(ssl, e);
        switch (e) {
        case SSL_ERROR_WANT_READ:
            buf->setSize(0);
            state = SSLReadWaitingForRead;
            return -EAGAIN;
        case SSL_ERROR_WANT_WRITE:
            buf->setSize(0);
            state = SSLReadWaitingForWrite;
            return -EAGAIN;
        case SSL_ERROR_ZERO_RETURN:
            return 0;
        }
        char msg[1024];
        ERR_error_string_n(ERR_get_error(), msg, sizeof(msg));
        Log(Log::Error) << "ssl error (processReadTLS)" << e << msg;
        return -e;
    };

    int e = -1;
    if (mFd4 != -1) {
        assert(mFd6 == -1);
        if (mMode == Plain) {
            e = processReadPlain(mFd4, buf, bytes);
        } else {
            e = processReadTLS(mSsl.session, buf, bytes, mSsl.readWaitState);
            if (mSsl.readWaitState == SSLReadWaitingForWrite) {
                event::Loop::loop()->updateFd(mFd4, event::Loop::FdRead | event::Loop::FdWrite);
            }
        }
    } else if (mFd6 != -1) {
        assert(mFd4 == -1);
        if (mMode == Plain) {
            e = processReadPlain(mFd6, buf, bytes);
        } else {
            e = processReadTLS(mSsl.session, buf, bytes, mSsl.readWaitState);
            if (mSsl.readWaitState == SSLReadWaitingForWrite) {
                event::Loop::loop()->updateFd(mFd6, event::Loop::FdRead | event::Loop::FdWrite);
            }
        }
    }
    if (e > 0) {
        return buf;
    } else if (!e) {
        close();

        mState = Closed;
        mStateChanged.emit(Closed);

        return std::shared_ptr<buffer::Buffer>();
    } else if (e == -EAGAIN) {
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
