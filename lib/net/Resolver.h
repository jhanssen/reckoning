#ifndef RESOLVER_H
#define RESOLVER_H

#include <event/Signal.h>
#include <net/IPAddress.h>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <string>
#include <vector>

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
    void shutdown();

    static Resolver& resolver();

private:
    Resolver(const Resolver&) = delete;
    Resolver& operator=(const Resolver&) = delete;

    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::vector<std::shared_ptr<Response> > mRequests;
    bool mStopped;
};

inline Resolver::Response::Response(const std::string& hostname)
    : mHostname(hostname)
{
}

inline const std::string& Resolver::Response::hostname() const
{
    return mHostname;
}

inline event::Signal<IPv4&&>& Resolver::Response::onIPv4()
{
    return mIPv4;
}

inline event::Signal<IPv6&&>& Resolver::Response::onIPv6()
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
