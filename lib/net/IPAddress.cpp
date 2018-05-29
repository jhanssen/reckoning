#include "IPAddress.h"
#include <ares.h>

using namespace reckoning;
using namespace reckoning::net;

std::string IPv4::name() const
{
    char addrbuf[16];
    ares_inet_ntop(AF_INET, &mIp, addrbuf, sizeof(addrbuf));
    return addrbuf;
}

std::string IPv6::name() const
{
    char addrbuf[46];
    ares_inet_ntop(AF_INET6, &mIp, addrbuf, sizeof(addrbuf));
    return addrbuf;
}
