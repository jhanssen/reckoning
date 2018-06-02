#ifndef TCPSOCKET_H
#define TCPSOCKET_H

#include <event/Loop.h>
#include <event/Signal.h>
#include <net/Resolver.h>
#include <net/IPAddress.h>
#include <buffer/Buffer.h>
#include <util/Creatable.h>
#include <memory>
#include <string>

namespace reckoning {
namespace net {

class TcpServer;

class TcpSocket : public std::enable_shared_from_this<TcpSocket>, public util::Creatable<TcpSocket>
{
public:
    enum { BufferSize = 16384 };

    ~TcpSocket();

    void connect(const std::string& host, uint16_t port);
    void connect(const IPv4& ip, uint16_t port);
    void connect(const IPv6& ip, uint16_t port);
    void close();

    void write(std::shared_ptr<buffer::Buffer>&& buffer);
    void write(const std::shared_ptr<buffer::Buffer>& buffer);
    void write(const uint8_t* data, size_t bytes);
    void write(const char* data, size_t bytes);

    enum State {
        Idle,
        Resolving,
        Resolved,
        Connecting,
        Connected,
        Closed,
        Error
    };
    event::Signal<State>& onStateChanged();
    event::Signal<std::shared_ptr<buffer::Buffer>&&>& onData();
    State state() const;

protected:
    TcpSocket();

private:
    void socketCallback(int fd, uint8_t flags);
    void processWrite(int fd);
    std::shared_ptr<buffer::Buffer> read(size_t bytes = BufferSize);

    void setSocket(int fd, bool ipv6);

private:
    int mFd4, mFd6;
    size_t mWriteOffset;
    std::vector<std::shared_ptr<buffer::Buffer> > mPendingWrites;
    std::shared_ptr<Resolver::Response> mResolver;
    event::Loop::FD mFd4Handle, mFd6Handle;
    event::Signal<State> mStateChanged;
    event::Signal<std::shared_ptr<buffer::Buffer>&&> mData;
    State mState;

    friend class TcpServer;
};

inline event::Signal<TcpSocket::State>& TcpSocket::onStateChanged()
{
    return mStateChanged;
}

inline event::Signal<std::shared_ptr<buffer::Buffer>&&>& TcpSocket::onData()
{
    return mData;
}

inline TcpSocket::State TcpSocket::state() const
{
    return mState;
}

inline void TcpSocket::write(const std::shared_ptr<buffer::Buffer>& buffer)
{
    mPendingWrites.push_back(buffer);
    if (mState != Connected)
        return;
    if (mFd4 != -1) {
        assert(mFd6 == -1);
        processWrite(mFd4);
    } else if (mFd6 != -1) {
        assert(mFd4 == -1);
        processWrite(mFd6);
    } else {
        assert(false && "No fd open for write?");
    }
}

inline void TcpSocket::write(std::shared_ptr<buffer::Buffer>&& buffer)
{
    mPendingWrites.push_back(std::forward<std::shared_ptr<buffer::Buffer> >(buffer));
    if (mState != Connected)
        return;
    if (mFd4 != -1) {
        assert(mFd6 == -1);
        processWrite(mFd4);
    } else if (mFd6 != -1) {
        assert(mFd4 == -1);
        processWrite(mFd6);
    } else {
        assert(false && "No fd open for write?");
    }
}

inline void TcpSocket::write(const char* data, size_t bytes)
{
    return write(reinterpret_cast<const uint8_t*>(data), bytes);
}

}} // namespace reckoning::net

#endif
