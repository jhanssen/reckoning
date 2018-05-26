#include "TcpSocket.h"
#include <log/Log.h>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;

TcpSocket::TcpSocket()
    : mFd(-1), mWriteOffset(0)
{
}

TcpSocket::~TcpSocket()
{
}

void TcpSocket::connect(const std::string& host, uint16_t port)
{
    mResolver = std::make_shared<Resolver::Response>(host);
    mResolver->onIPv4().connect([this, port](Resolver::Response::IPv4&& ip) {
            Log(Log::Info) << "resolved to" << ip.name();
        });
    mResolver->onIPv6().connect([this, port](Resolver::Response::IPv6&& ip) {
            Log(Log::Info) << "resolved to" << ip.name();
        });
    mResolver->onError().connect([this](std::string&& msg) {
            mResolver.reset();
        });
    mResolver->onComplete().connect([this]() {
            mResolver.reset();
        });
    Resolver::resolver().startRequest(mResolver);
}
