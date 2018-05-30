#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <config.h>
#include <util/Creatable.h>
#include <util/Invocable.h>
#include <type_traits>
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

class EventLoop : public std::enable_shared_from_this<EventLoop>, public util::Creatable<EventLoop>
{
public:
    ~EventLoop();

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

        friend class EventLoop;
    };

    template<typename T, typename ...Args>
    typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, void>::type
    send(T&& func, Args&& ...args);

    template<typename T, typename std::enable_if<std::is_base_of<Event, T>::value, T>::type* = nullptr>
    void send(T&& event);

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
        std::shared_ptr<EventLoop> loop() const { return mLoop.lock(); }

        void stop();

    protected:
        virtual void execute() = 0;

    private:
        std::chrono::milliseconds mTimeout;
        std::chrono::time_point<std::chrono::steady_clock> mNext;
        TimerFlag mFlag;
        std::weak_ptr<EventLoop> mLoop;

        friend class EventLoop;
    };

    template<typename T, typename ...Args>
    typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, std::shared_ptr<Timer> >::type
    timer(std::chrono::milliseconds timeout, T&& func, Args&& ...args);

    template<typename T, typename ...Args>
    typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, std::shared_ptr<Timer> >::type
    timer(std::chrono::milliseconds timeout, TimerFlag flag, T&& func, Args&& ...args);

    template<typename T, typename std::enable_if<std::is_base_of<Timer, T>::value, T>::type* = nullptr>
    std::shared_ptr<Timer> timer(T&& timer);

    void timer(const std::shared_ptr<Timer>& timer);

    // file descriptor
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
        std::weak_ptr<EventLoop> mLoop;

        friend class EventLoop;
    };

    enum FdFlag { FdError = 0x1, FdRead = 0x2, FdWrite = 0x4 };
    template<typename T, typename std::enable_if<std::is_invocable_r<void, T, int, uint8_t>::value, T>::type* = nullptr>
    FD fd(int fd, uint8_t flags, T&& callback);
    void fd(int fd, uint8_t flags);

    int execute(std::chrono::milliseconds timeout = std::chrono::milliseconds{-1});
    void exit(int status = 0);

    static std::shared_ptr<EventLoop> loop();

protected:
    EventLoop();

private:
    void destroy();
    void wakeup();
    void cleanup();

private:
#ifdef HAVE_KQUEUE
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
    int mStatus;
    bool mStopped;

    thread_local static std::weak_ptr<EventLoop> tLoop;

    friend class Timer;
};

inline bool EventLoop::isLoopThread() const
{
    return mThread == std::this_thread::get_id();
}

inline void EventLoop::Timer::stop()
{
    std::shared_ptr<EventLoop> loop = mLoop.lock();
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

namespace detail {
template<typename T, typename ...Args>
class ArgsEvent : public EventLoop::Event
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
class ArgsTimer : public EventLoop::Timer
{
public:
    ArgsTimer(std::chrono::milliseconds timeout, EventLoop::TimerFlag flag, T&& f, Args&& ...a)
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
EventLoop::send(T&& func, Args&& ...args)
{
    if (mThread == std::this_thread::get_id()) {
        func(std::forward<Args>(args)...);
    } else {
        std::unique_ptr<detail::ArgsEvent<T, Args...> > event = std::make_unique<detail::ArgsEvent<T, Args...> >(std::forward<T>(func), std::forward<Args>(args)...);
        post(std::move(event));
    }
}

template<typename T, typename std::enable_if<std::is_base_of<EventLoop::Event, T>::value, T>::type*>
inline void EventLoop::send(T&& event)
{
    if (mThread == std::this_thread::get_id()) {
        event.execute();
    } else {
        send(std::make_shared<Event>(new T(std::forward<T>(event))));
    }
}

inline void EventLoop::send(std::unique_ptr<Event>&& event)
{
    if (mThread == std::this_thread::get_id()) {
        event->execute();
    } else {
        post(std::forward<std::unique_ptr<Event> >(event));
    }
}

inline void EventLoop::post(std::unique_ptr<Event>&& event)
{
    std::lock_guard<std::mutex> locker(mMutex);
    mEvents.push_back(std::forward<std::unique_ptr<Event> >(event));
    wakeup();
}

template<typename T, typename ...Args>
typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, std::shared_ptr<EventLoop::Timer> >::type
EventLoop::timer(std::chrono::milliseconds timeout, T&& func, Args&& ...args)
{
    auto st = std::make_shared<detail::ArgsTimer<T, Args...> >(timeout, Timeout, std::forward<T>(func), std::forward<Args>(args)...);
    st->mLoop = shared_from_this();
    timer(st);
    return st;
}

template<typename T, typename ...Args>
typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, std::shared_ptr<EventLoop::Timer> >::type
EventLoop::timer(std::chrono::milliseconds timeout, TimerFlag flag, T&& func, Args&& ...args)
{
    auto st = std::make_shared<detail::ArgsTimer<T, Args...> >(timeout, flag, std::forward<T>(func), std::forward<Args>(args)...);
    st->mLoop = shared_from_this();
    timer(st);
    return st;
}

template<typename T, typename std::enable_if<std::is_base_of<EventLoop::Timer, T>::value, T>::type*>
inline std::shared_ptr<EventLoop::Timer> EventLoop::timer(T&& t)
{
    auto st = std::make_shared<Timer>(new T(std::forward<T>(t)));
    st->mLoop = shared_from_this();
    timer(st);
    return st;
}

inline void EventLoop::timer(const std::shared_ptr<Timer>& t)
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
inline EventLoop::FD EventLoop::fd(int fd, uint8_t flags, T&& callback)
{
    FD r;
    r.mFd = fd;
    r.mLoop = shared_from_this();

    std::lock_guard<std::mutex> locker(mMutex);
    mPendingFds.push_back(std::make_pair(fd, std::forward<T>(callback)));
    if (flags & FdWrite) {
        mUpdateFds.push_back(std::make_pair(fd, FdWrite | ((flags & FdRead) ? FdRead : 0)));
    }
    wakeup();

    return r;
}

inline void EventLoop::fd(int fd, uint8_t flags)
{
    std::lock_guard<std::mutex> locker(mMutex);
    mUpdateFds.push_back(std::make_pair(fd, flags));
    wakeup();
}

inline EventLoop::FD::FD()
    : mFd(-1)
{
}

inline EventLoop::FD::FD(FD&& other)
    : mFd(other.mFd), mLoop(std::move(other.mLoop))
{
}

inline EventLoop::FD& EventLoop::FD::operator=(FD&& other)
{
    mFd = other.mFd;
    mLoop = std::move(other.mLoop);
    return *this;
}

inline void EventLoop::FD::remove()
{
    auto loop = mLoop.lock();
    if (!loop)
        return;
    std::lock_guard<std::mutex> locker(loop->mMutex);
    auto it = loop->mFds.begin();
    auto end = loop->mFds.cend();
    while (it != end) {
        if (it->first == mFd) {
            loop->mFds.erase(it);
            break;
        }
        ++it;
    }
    it = loop->mPendingFds.begin();
    end = loop->mPendingFds.cend();
    while (it != end) {
        if (it->first == mFd) {
            loop->mPendingFds.erase(it);
            break;
        }
        ++it;
    }
    // remove from mUpdateFds if it's there
    auto uit = loop->mPendingFds.begin();
    const auto uend = loop->mPendingFds.cend();
    while (uit != uend) {
        if (uit->first == mFd) {
            loop->mPendingFds.erase(uit);
            break;
        }
        ++uit;
    }
    loop->mRemovedFds.push_back(mFd);
    loop->wakeup();
}

inline std::shared_ptr<EventLoop> EventLoop::loop()
{
    return tLoop.lock();
}

}} // namespace reckoning::event

#endif
