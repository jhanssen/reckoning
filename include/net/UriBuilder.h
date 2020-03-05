#ifndef URIBUILDER_H
#define URIBUILDER_H

#include <string>
#include <net/UriParser.h>

namespace reckoning {
namespace net {
namespace uri {

inline std::string path(const std::string& path, const std::string& query = std::string(), const std::string& fragment = std::string())
{
    return path + (query.empty() ? std::string() : ("?" + query)) + (fragment.empty() ? std::string() : ("#" + fragment));
}

inline std::string path(const UriParser& uri)
{
    return path(uri.path(), uri.query(), uri.fragment());
}

inline std::string build(const std::string& scheme, const std::string& host, const std::string& port, const std::string& path,
                         const std::string& query = std::string(), const std::string& fragment = std::string())
{
    return scheme + "://" + host + (port.empty() ? std::string() : (":" + port)) + reckoning::net::uri::path(path, query, fragment);
}

inline std::string build(const UriParser& uri)
{
    return build(uri.scheme(), uri.host(), uri.port(), uri.path(), uri.query(), uri.fragment());
}

}}} // namespace reckoning::net::uri

#endif
