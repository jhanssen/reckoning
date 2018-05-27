#ifndef TCPSOCKET_H
#define TCPSOCKET_H

#include <buffer/Pool.h>
#include <event/EventLoop.h>
#include <event/Signal.h>
#include <net/Resolver.h>
#include <memory>
#include <string>

namespace reckoning {
namespace net {

class TcpSocket : public std::enable_shared_from_this<TcpSocket>
{
public:
    enum { BufferSize = 16384 };

    TcpSocket();
    ~TcpSocket();

    void connect(const std::string& host, uint16_t port);
    void connect(const Resolver::Response::IPv4& ip, uint16_t port);
    void connect(const Resolver::Response::IPv6& ip, uint16_t port);
    void close();

    std::shared_ptr<buffer::Buffer<BufferSize> > read(size_t bytes = BufferSize);
    void write(std::shared_ptr<buffer::Buffer<BufferSize> >&& buffer);

    enum State {
        Idle,
        Resolving,
        Resolved,
        Connecting,
        Connected,
        Closed,
        ReadyRead,
        Error
    };
    event::Signal<State>& stateChanged();
    State state() const;

private:
    void socketCallback(int fd, uint8_t flags);
    void processWrite(int fd);

private:
    int mFd4, mFd6;
    size_t mWriteOffset;
    std::vector<std::shared_ptr<buffer::Buffer<BufferSize> > > mPendingWrites;
    std::shared_ptr<Resolver::Response> mResolver;
    event::EventLoop::FD mFd4Handle, mFd6Handle;
    event::Signal<State> mStateChanged;
    State mState;
};

inline event::Signal<TcpSocket::State>& TcpSocket::stateChanged()
{
    return mStateChanged;
}

inline TcpSocket::State TcpSocket::state() const
{
    return mState;
}

inline void TcpSocket::write(std::shared_ptr<buffer::Buffer<BufferSize> >&& buffer)
{
    mPendingWrites.push_back(std::forward<std::shared_ptr<buffer::Buffer<BufferSize> > >(buffer));
    if (mState != Connected)
        return;
    if (mFd4 != -1) {
        assert(mFd6 == -1);
        processWrite(mFd4);
    } else if (mFd6 != -1) {
        assert(mFd4 == -1);
        processWrite(mFd6);
    } else {
        assert(false && "No fd open?");
    }
}

}} // namespace reckoning::net

#endif
