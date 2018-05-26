#ifndef RESOLVER_H
#define RESOLVER_H

#include <event/Signal.h>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <string>
#include <vector>
#include <netinet/in.h>

namespace reckoning {
namespace net {

class Resolver
{
public:
    Resolver();

    class Response
    {
    public:
        Response(const std::string& hostname);

        const std::string& hostname() const;

        class IPv4
        {
        public:
            IPv4(const in_addr& in);

            const in_addr& ip() const;
            std::string name() const;

        private:
            in_addr mIp;
        };

        class IPv6
        {
        public:
            IPv6(const in6_addr& in);

            const in6_addr& ip() const;
            std::string name() const;

        private:
            in6_addr mIp;
        };

        event::Signal<IPv4&&>& onIPv4();
        event::Signal<IPv6&&>& onIPv6();
        event::Signal<>& onComplete();
        event::Signal<std::string&&>& onError();

    private:
        std::string mHostname;
        event::Signal<IPv4&&> mIPv4;
        event::Signal<IPv6&&> mIPv6;
        event::Signal<> mComplete;
        event::Signal<std::string&&> mError;

        friend class Resolver;
    };

    void startRequest(const std::shared_ptr<Response>& request);

    static Resolver& resolver();

private:
    Resolver(const Resolver&) = delete;
    Resolver& operator=(const Resolver&) = delete;

    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::vector<std::shared_ptr<Response> > mRequests;
};

inline Resolver::Response::Response(const std::string& hostname)
    : mHostname(hostname)
{
}

inline Resolver::Response::IPv4::IPv4(const in_addr& in)
{
    mIp = in;
}

inline const in_addr& Resolver::Response::IPv4::ip() const
{
    return mIp;
}

inline Resolver::Response::IPv6::IPv6(const in6_addr& in)
{
    mIp = in;
}

inline const in6_addr& Resolver::Response::IPv6::ip() const
{
    return mIp;
}

inline const std::string& Resolver::Response::hostname() const
{
    return mHostname;
}

inline event::Signal<Resolver::Response::IPv4&&>& Resolver::Response::onIPv4()
{
    return mIPv4;
}

inline event::Signal<Resolver::Response::IPv6&&>& Resolver::Response::onIPv6()
{
    return mIPv6;
}

inline event::Signal<>& Resolver::Response::onComplete()
{
    return mComplete;
}

inline event::Signal<std::string&&>& Resolver::Response::onError()
{
    return mError;
}

inline void Resolver::startRequest(const std::shared_ptr<Resolver::Response>& request)
{
    std::lock_guard<std::mutex> locker(mMutex);
    mRequests.push_back(request);
    mCondition.notify_one();
}

inline Resolver& Resolver::resolver()
{
    static Resolver sResolver;
    return sResolver;
}

}} // namespace reckoning::net

#endif
