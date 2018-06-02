#ifndef SOCKETUTIL_H
#define SOCKETUTIL_H

#include <fcntl.h>
#include <cerrno>

#define eintrwrap(VAR, BLOCK)                   \
    do {                                        \
        VAR = BLOCK;                            \
    } while (VAR == -1 && errno == EINTR)

namespace reckoning {
namespace util {
namespace socket {

inline bool setFlag(int fd, int flag)
{
    int e, f;
    eintrwrap(e, fcntl(fd, F_GETFL, 0));
    if (e == -1) {
        return false;
    }
    eintrwrap(f, fcntl(fd, F_SETFL, e | flag));
    if (f == -1) {
        return false;
    }
    return true;
}

}}} // namespace reckoning::util::socket

#endif // SOCKETUTIL_H
