#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <config.h>
#include <util/Creatable.h>
#include <util/Invocable.h>
#include <cassert>
#include <type_traits>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <chrono>
#include <mutex>
#include <functional>

#if defined(HAVE_KQUEUE)
#  include <sys/types.h>
#  include <sys/event.h>
#  include <sys/time.h>
#elif defined(HAVE_EPOLL)
#  include <sys/epoll.h>
#endif

namespace reckoning {
namespace event {

class Loop : public std::enable_shared_from_this<Loop>, public util::Creatable<Loop>
{
public:
    ~Loop();

    void init();

    bool isLoopThread() const;

    // Events
    class Event
    {
    public:
        Event() { }
        virtual ~Event() { }

    protected:
        virtual void execute() = 0;

        friend class Loop;
    };

    template<typename T, typename ...Args>
    typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, void>::type
    send(T&& func, Args&& ...args);

    template<typename T, typename std::enable_if<std::is_base_of<Event, T>::value, T>::type* = nullptr>
    void send(T&& event);

    template<typename T, typename ...Args>
    typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, void>::type
    post(T&& func, Args&& ...args);

    template<typename T, typename std::enable_if<std::is_base_of<Event, T>::value, T>::type* = nullptr>
    void post(T&& event);

    void send(std::unique_ptr<Event>&& event);
    void post(std::unique_ptr<Event>&& event);

    // Timers
    enum TimerFlag { Timeout, Interval };

    class Timer
    {
    public:
        Timer(std::chrono::milliseconds timeout, TimerFlag flag = Timeout) : mTimeout(timeout), mFlag(flag) { }
        virtual ~Timer() { }

        std::chrono::milliseconds timeout() const { return mTimeout; }
        std::chrono::time_point<std::chrono::steady_clock> next() const { return mNext; }

        TimerFlag flag() const { return mFlag; }
        std::shared_ptr<Loop> loop() const { return mLoop.lock(); }

        void updateTimeout(std::chrono::milliseconds timeout) { assert(!isActive()); mTimeout = timeout; }

        void stop();
        bool isActive() const;

    protected:
        virtual void execute() = 0;

    private:
        std::chrono::milliseconds mTimeout;
        std::chrono::time_point<std::chrono::steady_clock> mNext;
        TimerFlag mFlag;
        std::weak_ptr<Loop> mLoop;

        friend class Loop;
    };

    template<typename T, typename ...Args>
    typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, std::shared_ptr<Timer> >::type
    addTimer(std::chrono::milliseconds timeout, T&& func, Args&& ...args);

    template<typename T, typename ...Args>
    typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, std::shared_ptr<Timer> >::type
    addTimer(std::chrono::milliseconds timeout, TimerFlag flag, T&& func, Args&& ...args);

    void addTimer(std::shared_ptr<Timer>&& timer);
    void addTimer(const std::shared_ptr<Timer>& timer);

    // File descriptors
    class FD
    {
    public:
        FD();
        FD(FD&& other);
        FD& operator=(FD&& other);

        void remove();

    private:
        FD(const FD&) = delete;
        FD& operator=(const FD&) = delete;

        int mFd;
        std::weak_ptr<Loop> mLoop;

        friend class Loop;
    };

    enum FdFlag { FdError = 0x1, FdRead = 0x2, FdWrite = 0x4 };
    template<typename T, typename std::enable_if<std::is_invocable_r<void, T, int, uint8_t>::value, T>::type* = nullptr>
    FD addFd(int fd, uint8_t flags, T&& callback);
    template<typename T, typename std::enable_if<std::is_invocable_r<void, T, int, uint8_t>::value, T>::type* = nullptr>
    FD addFd(int fd, uint8_t flags, const T& callback);
    void updateFd(int fd, uint8_t flags);
    void removeFd(int fd);

    int execute(std::chrono::milliseconds timeout = std::chrono::milliseconds{-1});
    void exit(int status = 0);

    bool stopped() const { return mStopped.load(std::memory_order_acquire); }

    static std::shared_ptr<Loop> loop();

protected:
    Loop();

private:
    // nice, three different destroy functions
    void destroy();
    void deinit();
    void cleanup();

    void wakeup(bool forceWrite = false);

    void commonInit();

private:
#if defined(HAVE_KQUEUE) || defined(HAVE_EPOLL)
    int mFd;
    int mWakeup[2];
#endif
    std::thread::id mThread;
    std::mutex mMutex;
    std::vector<std::unique_ptr<Event> > mEvents;
    std::vector<std::shared_ptr<Timer> > mTimers;
    std::vector<std::pair<int, std::function<void(int, uint8_t)> > > mFds, mPendingFds;
    std::vector<std::pair<int, uint8_t> > mUpdateFds;
    std::vector<int> mRemovedFds;
    std::atomic<bool> mStopped;
    int mStatus;

    thread_local static std::weak_ptr<Loop> tLoop;

    friend class Timer;
};

inline bool Loop::isLoopThread() const
{
    return mThread == std::this_thread::get_id();
}

inline void Loop::Timer::stop()
{
    std::shared_ptr<Loop> loop = mLoop.lock();
    if (!loop)
        return;

    std::lock_guard<std::mutex> locker(loop->mMutex);
    auto it = loop->mTimers.begin();
    const auto end = loop->mTimers.cend();
    while (it != end) {
        if (it->get() == this) {
            // got it
            loop->mTimers.erase(it);
            return;
        }
        ++it;
    }
}

inline bool Loop::Timer::isActive() const
{
    std::shared_ptr<Loop> loop = mLoop.lock();
    if (!loop)
        return false;

    std::lock_guard<std::mutex> locker(loop->mMutex);
    auto it = loop->mTimers.begin();
    const auto end = loop->mTimers.cend();
    while (it != end) {
        if (it->get() == this)
            return true;
        ++it;
    }
    return false;
}


namespace detail {
template<typename T, typename ...Args>
class ArgsEvent : public Loop::Event
{
public:
    ArgsEvent(T&& f, Args&& ...a) : func(std::forward<T>(f)), args(std::make_tuple(std::forward<Args>(a)...)) { }
    ArgsEvent(ArgsEvent<T, Args...>&& other) : func(std::move(other.func)), args(std::move(other.args)) { }
    ~ArgsEvent() override { }

    ArgsEvent<T, Args...>& operator=(ArgsEvent<T, Args...>&& other) { func = std::move(other.func); args = std::move(other.args); return *this; }

    virtual void execute() override { std::apply(func, args); }

private:
    ArgsEvent(const ArgsEvent&) = delete;
    ArgsEvent& operator=(const ArgsEvent&) = delete;

    std::function<void(typename std::decay<Args>::type...)> func;
    std::tuple<typename std::decay<Args>::type...> args;
};

template<typename T, typename ...Args>
class ArgsTimer : public Loop::Timer
{
public:
    ArgsTimer(std::chrono::milliseconds timeout, Loop::TimerFlag flag, T&& f, Args&& ...a)
        : Timer(timeout, flag), func(std::forward<T>(f)), args(std::make_tuple(std::forward<Args>(a)...))
    { }
    ArgsTimer(ArgsTimer<T, Args...>&& other) : func(std::move(other.func)), args(std::move(other.args)) { }
    ~ArgsTimer() override { }

    ArgsTimer<T, Args...>& operator=(ArgsTimer<T, Args...>&& other) { func = std::move(other.func); args = std::move(other.args); return *this; }

    virtual void execute() override { std::apply(func, args); }

private:
    ArgsTimer(const ArgsTimer&) = delete;
    ArgsTimer& operator=(const ArgsTimer&) = delete;

    std::function<void(typename std::decay<Args>::type...)> func;
    std::tuple<typename std::decay<Args>::type...> args;
};
} // namespace detail

template<typename T, typename ...Args>
inline typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, void>::type
Loop::send(T&& func, Args&& ...args)
{
    if (mThread == std::this_thread::get_id()) {
        func(std::forward<Args>(args)...);
    } else {
        std::unique_ptr<detail::ArgsEvent<T, Args...> > event = std::make_unique<detail::ArgsEvent<T, Args...> >(std::forward<T>(func), std::forward<Args>(args)...);
        post(std::move(event));
    }
}

template<typename T, typename std::enable_if<std::is_base_of<Loop::Event, T>::value, T>::type*>
inline void Loop::send(T&& event)
{
    if (mThread == std::this_thread::get_id()) {
        event.execute();
    } else {
        send(std::make_unique<Event>(new T(std::forward<T>(event))));
    }
}

inline void Loop::send(std::unique_ptr<Event>&& event)
{
    if (mThread == std::this_thread::get_id()) {
        event->execute();
    } else {
        post(std::move(event));
    }
}

template<typename T, typename ...Args>
inline typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, void>::type
Loop::post(T&& func, Args&& ...args)
{
    std::unique_ptr<detail::ArgsEvent<T, Args...> > event = std::make_unique<detail::ArgsEvent<T, Args...> >(std::forward<T>(func), std::forward<Args>(args)...);
    post(std::move(event));
}

template<typename T, typename std::enable_if<std::is_base_of<Loop::Event, T>::value, T>::type*>
inline void Loop::post(T&& event)
{
    post(std::make_unique<Event>(new T(std::forward<T>(event))));
}

inline void Loop::post(std::unique_ptr<Event>&& event)
{
    std::lock_guard<std::mutex> locker(mMutex);
    mEvents.push_back(std::move(event));
    wakeup();
}

template<typename T, typename ...Args>
typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, std::shared_ptr<Loop::Timer> >::type
Loop::addTimer(std::chrono::milliseconds timeout, T&& func, Args&& ...args)
{
    auto st = std::make_shared<detail::ArgsTimer<T, Args...> >(timeout, Timeout, std::forward<T>(func), std::forward<Args>(args)...);
    st->mLoop = shared_from_this();
    addTimer(st);
    return st;
}

template<typename T, typename ...Args>
typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, std::shared_ptr<Loop::Timer> >::type
Loop::addTimer(std::chrono::milliseconds timeout, TimerFlag flag, T&& func, Args&& ...args)
{
    auto st = std::make_shared<detail::ArgsTimer<T, Args...> >(timeout, flag, std::forward<T>(func), std::forward<Args>(args)...);
    st->mLoop = shared_from_this();
    addTimer(st);
    return st;
}

inline void Loop::addTimer(std::shared_ptr<Timer>&& t)
{
    t->mNext = std::chrono::steady_clock::now() + t->mTimeout;
    t->mLoop = shared_from_this();

    std::lock_guard<std::mutex> locker(mMutex);

    // we need our timer list to be sorted
    auto compare = [](const std::shared_ptr<Timer>& a, const std::shared_ptr<Timer>& b) -> bool {
        return a->mNext < b->mNext;
    };
    auto it = std::lower_bound(mTimers.begin(), mTimers.end(), t, compare);
    mTimers.insert(it, std::move(t));

    wakeup();
}

inline void Loop::addTimer(const std::shared_ptr<Timer>& t)
{
    t->mNext = std::chrono::steady_clock::now() + t->mTimeout;
    t->mLoop = shared_from_this();

    std::lock_guard<std::mutex> locker(mMutex);

    // we need our timer list to be sorted
    auto compare = [](const std::shared_ptr<Timer>& a, const std::shared_ptr<Timer>& b) -> bool {
        return a->mNext < b->mNext;
    };
    auto it = std::lower_bound(mTimers.begin(), mTimers.end(), t, compare);
    mTimers.insert(it, t);

    wakeup();
}

template<typename T, typename std::enable_if<std::is_invocable_r<void, T, int, uint8_t>::value, T>::type*>
inline Loop::FD Loop::addFd(int fd, uint8_t flags, T&& callback)
{
    assert(!(flags & FdError));

    FD r;
    r.mFd = fd;
    r.mLoop = shared_from_this();

    std::lock_guard<std::mutex> locker(mMutex);
    mPendingFds.push_back(std::make_pair(fd, std::forward<T>(callback)));
    if (flags & FdWrite) {
        mUpdateFds.push_back(std::make_pair(fd, FdWrite | ((flags & FdRead) ? FdRead : 0)));
    }
    // take it out of mRemovedFds if it exists
    auto rit = std::find(mRemovedFds.begin(), mRemovedFds.end(), fd);
    if (rit != mRemovedFds.end()) {
        mRemovedFds.erase(rit);
    }
    wakeup();

    return r;
}

template<typename T, typename std::enable_if<std::is_invocable_r<void, T, int, uint8_t>::value, T>::type*>
inline Loop::FD Loop::addFd(int fd, uint8_t flags, const T& callback)
{
    assert(!(flags & FdError));

    FD r;
    r.mFd = fd;
    r.mLoop = shared_from_this();

    std::lock_guard<std::mutex> locker(mMutex);
    mPendingFds.push_back(std::make_pair(fd, callback));
    if (flags & FdWrite) {
        mUpdateFds.push_back(std::make_pair(fd, FdWrite | ((flags & FdRead) ? FdRead : 0)));
    }
    // take it out of mRemovedFds if it exists
    auto rit = std::find(mRemovedFds.begin(), mRemovedFds.end(), fd);
    if (rit != mRemovedFds.end()) {
        mRemovedFds.erase(rit);
    }
    wakeup();

    return r;
}

inline void Loop::updateFd(int fd, uint8_t flags)
{
    assert(!(flags & FdError));

    std::lock_guard<std::mutex> locker(mMutex);
    mUpdateFds.push_back(std::make_pair(fd, flags));
    wakeup();
}

inline Loop::FD::FD()
    : mFd(-1)
{
}

inline Loop::FD::FD(FD&& other)
    : mFd(other.mFd), mLoop(std::move(other.mLoop))
{
}

inline Loop::FD& Loop::FD::operator=(FD&& other)
{
    mFd = other.mFd;
    mLoop = std::move(other.mLoop);
    return *this;
}

inline void Loop::removeFd(int fd)
{
    std::lock_guard<std::mutex> locker(mMutex);
    auto it = mFds.begin();
    auto end = mFds.cend();
    while (it != end) {
        if (it->first == fd) {
            mFds.erase(it);
            break;
        }
        ++it;
    }
    it = mPendingFds.begin();
    end = mPendingFds.cend();
    while (it != end) {
        if (it->first == fd) {
            mPendingFds.erase(it);
            break;
        }
        ++it;
    }
    // remove from mUpdateFds if it's there
    auto uit = mUpdateFds.begin();
    const auto uend = mUpdateFds.cend();
    while (uit != uend) {
        if (uit->first == fd) {
            mUpdateFds.erase(uit);
            break;
        }
        ++uit;
    }
    mRemovedFds.push_back(fd);
    wakeup();
}

inline void Loop::FD::remove()
{
    if (mFd == -1)
        return;
    auto loop = mLoop.lock();
    if (!loop)
        return;
    loop->removeFd(mFd);
    mFd = -1;
}

inline std::shared_ptr<Loop> Loop::loop()
{
    return tLoop.lock();
}

static inline int64_t timeNow()
{
    return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()).time_since_epoch().count();
}

}} // namespace reckoning::event

#endif
