#include "Resolver.h"
#include <event/EventLoop.h>
#include <log/Log.h>
#include <ares.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

using namespace reckoning;
using namespace reckoning::net;
using namespace reckoning::log;
using namespace std::chrono_literals;

Resolver::Resolver()
    : mStopped(false)
{
    mThread = std::thread([this]() {
            auto callback = [](void *arg, int status, int timeouts, struct hostent *host) {
                Resolver::Response* response = static_cast<Resolver::Response*>(arg);
                if (status != ARES_SUCCESS) {
                    response->mError.emit("error getting response " + std::to_string(status));
                    return;
                }
                //char addr_buf[46];
                for (char** p = host->h_addr_list; *p; ++p) {
                    //ares_inet_ntop(host->h_addrtype, *p, addr_buf, sizeof(addr_buf));
                    switch (host->h_addrtype) {
                    case AF_INET: {
                        Resolver::Response::IPv4 ipv4(*reinterpret_cast<in_addr*>(*p));
                        response->mIPv4.emit(std::move(ipv4));
                        break; }
                    case AF_INET6: {
                        Resolver::Response::IPv6 ipv6(*reinterpret_cast<in6_addr*>(*p));
                        response->mIPv6.emit(std::move(ipv6));
                        break; }
                    default:
                        // whey
                        break;
                    }
                }
            };

            ares_channel channel;
            int e = ares_library_init(ARES_LIB_INIT_ALL);
            if (e != ARES_SUCCESS) {
                Log(Log::Error) << "Unable to set up c-ares library" << e;
                return;
            }
            e = ares_init(&channel);
            if (e != ARES_SUCCESS) {
                Log(Log::Error) << "Unable to set up c-ares channel" << e;
                return;
            }

            struct in_addr addr4;
            struct ares_in6_addr addr6;
            fd_set read_fds, write_fds;
            struct timeval *tvp, tv;
            int nfds;

            for (;;) {
                std::vector<std::shared_ptr<Response> > requests;
                {
                    std::unique_lock<std::mutex> locker(mMutex);
                    if (mStopped)
                        return;
                    while (mRequests.empty()) {
                        mCondition.wait_for(locker, 1000ms);
                        if (mStopped)
                            return;
                    }
                    requests = std::move(mRequests);
                }
                for (const auto& req : requests) {
                    if (ares_inet_pton(AF_INET, req->hostname().c_str(), &addr4) == 1) {
                        ares_gethostbyaddr(channel, &addr4, sizeof(addr4), AF_INET, callback, req.get());
                    } else if (ares_inet_pton(AF_INET6, req->hostname().c_str(), &addr6) == 1) {
                        ares_gethostbyaddr(channel, &addr6, sizeof(addr6), AF_INET6, callback, req.get());
                    } else {
                        ares_gethostbyname(channel, req->hostname().c_str(), AF_INET, callback, req.get());
                        ares_gethostbyname(channel, req->hostname().c_str(), AF_INET6, callback, req.get());
                    }
                }
                for (;;) {
                    FD_ZERO(&read_fds);
                    FD_ZERO(&write_fds);
                    nfds = ares_fds(channel, &read_fds, &write_fds);
                    if (!nfds) {
                        // done?
                        for (const auto& req : requests) {
                            req->mComplete.emit();
                        }
                        break;
                    }
                    tvp = ares_timeout(channel, nullptr, &tv);
                    e = select(nfds, &read_fds, &write_fds, nullptr, tvp);
                    if (e == -1) {
                        // ugh
                        Log(Log::Error) << "Error selecting c-ares" << e;
                        for (const auto& req : requests) {
                            req->mComplete.emit();
                        }
                        break;
                    }
                    ares_process(channel, &read_fds, &write_fds);
                }
            }
        });
}

void Resolver::shutdown()
{
    {
        std::unique_lock<std::mutex> locker(mMutex);
        mStopped = true;
    }
    mThread.join();
}

std::string Resolver::Response::IPv4::name() const
{
    char addrbuf[16];
    ares_inet_ntop(AF_INET, &mIp, addrbuf, sizeof(addrbuf));
    return addrbuf;
}

std::string Resolver::Response::IPv6::name() const
{
    char addrbuf[46];
    ares_inet_ntop(AF_INET6, &mIp, addrbuf, sizeof(addrbuf));
    return addrbuf;
}
