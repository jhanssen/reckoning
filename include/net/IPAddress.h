#ifndef IPADDRESS_H
#define IPADDRESS_H

#include <netinet/in.h>
#include <string>

namespace reckoning {
namespace net {

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

inline IPv4::IPv4(const in_addr& in)
{
    mIp = in;
}

inline const in_addr& IPv4::ip() const
{
    return mIp;
}

inline IPv6::IPv6(const in6_addr& in)
{
    mIp = in;
}

inline const in6_addr& IPv6::ip() const
{
    return mIp;
}

}} // namespace reckoning::net

#endif // IPADDRESS_H
