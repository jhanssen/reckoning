#ifndef TIMEVAL_H
#define TIMEVAL_H

#include <ctime>

// partly lifted from https://stackoverflow.com/questions/17402657/how-to-convert-stdchronosystem-clockduration-into-struct-timeval

namespace std {
namespace chrono {
namespace detail {

template<typename From, typename To>
struct posix_duration_cast;

// chrono -> timeval caster
template<typename Rep, typename Period>
struct posix_duration_cast< std::chrono::duration<Rep, Period>, struct timeval > {

    static struct timeval cast(std::chrono::duration<Rep, Period> const& d) {
        struct timeval tv;

        std::chrono::seconds const sec = std::chrono::duration_cast<std::chrono::seconds>(d);

        tv.tv_sec  = sec.count();
        tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(d - sec).count();

        return tv;
    }

};

// chrono -> timespec caster
template<typename Rep, typename Period>
struct posix_duration_cast< std::chrono::duration<Rep, Period>, struct timespec > {

    static struct timespec cast(std::chrono::duration<Rep, Period> const& d) {
        struct timespec ts;

        std::chrono::seconds const sec = std::chrono::duration_cast<std::chrono::seconds>(d);

        ts.tv_sec  = sec.count();
        ts.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(d - sec).count();

        return ts;
    }

};

// timeval -> chrono caster
template<typename Rep, typename Period>
struct posix_duration_cast< struct timeval, std::chrono::duration<Rep, Period> > {

    static std::chrono::duration<Rep, Period> cast(struct timeval const & tv) {
        return std::chrono::duration_cast< std::chrono::duration<Rep, Period> >(
            std::chrono::seconds(tv.tv_sec) + std::chrono::microseconds(tv.tv_usec)
            );
    }

};

// timespec -> chrono caster
template<typename Rep, typename Period>
struct posix_duration_cast< struct timespec, std::chrono::duration<Rep, Period> > {

    static std::chrono::duration<Rep, Period> cast(struct timespec const & ts) {
        return std::chrono::duration_cast< std::chrono::duration<Rep, Period> >(
            std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec)
            );
    }

};

}

// chrono -> timeval
template<typename T, typename Rep, typename Period>
auto duration_cast(std::chrono::duration<Rep, Period> const& d)
    -> std::enable_if_t< std::is_same<T, struct timeval>::value, struct timeval >
{
    return detail::posix_duration_cast< std::chrono::duration<Rep, Period>, timeval >::cast(d);
}

// chrono -> timespec
template<typename T, typename Rep, typename Period>
auto duration_cast(std::chrono::duration<Rep, Period> const& d)
    -> std::enable_if_t< std::is_same<T, struct timespec>::value, struct timespec >
{
    return detail::posix_duration_cast< std::chrono::duration<Rep, Period>, timespec >::cast(d);
}

// timeval -> chrono
template<typename Duration>
Duration duration_cast(struct timeval const& tv) {
    return detail::posix_duration_cast< struct timeval, Duration >::cast(tv);
}

// timespec -> chrono
template<typename Duration>
Duration duration_cast(struct timespec const& ts) {
    return detail::posix_duration_cast< struct timespec, Duration >::cast(ts);
}

} // chrono
} // std

#endif
