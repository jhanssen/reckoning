#ifndef TCPSOCKET_H
#define TCPSOCKET_H

#include <buffer/Pool.h>
#include <event/Signal.h>
#include <net/Resolver.h>
#include <string>

namespace reckoning {
namespace net {

class TcpSocket
{
public:
    enum { BufferSize = 16384 };

    TcpSocket();
    ~TcpSocket();

    void connect(const std::string& host, uint16_t port);
    void close();

    std::shared_ptr<buffer::Buffer<BufferSize> > read(size_t bytes = BufferSize);
    void write(const std::shared_ptr<buffer::Buffer<BufferSize> >& buffer);

    enum State {
        Resolved,
        Connected,
        Closed,
        ReadyRead,
        Error
    };
    event::Signal<State>& stateChanged();

private:
    int mFd;
    size_t mWriteOffset;
    std::vector<std::shared_ptr<buffer::Buffer<BufferSize> > > mPendingWrites;
    std::shared_ptr<Resolver::Response> mResolver;
    event::Signal<State> mStateChanged;
};

}} // namespace reckoning::net

#endif
