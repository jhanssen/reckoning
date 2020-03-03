#ifndef URIBUILDER_H
#define URIBUILDER_H

#include <string>

inline std::string buildUri(const std::string& scheme, const std::string& host, const std::string& port, const std::string& path,
                            const std::string& query = std::string(), const std::string& fragment = std::string())
{
    return scheme + "://" + host + (port.empty() ? std::string() : (":" + port)) + path + (query.empty() ? std::string() : ("?" + query)) + (fragment.empty() ? std::string() : ("#" + fragment));
}

#endif
