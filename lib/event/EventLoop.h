#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <config.h>
#include <type_traits>
#include <thread>
#include <memory>
#include <vector>

#ifdef HAVE_KQUEUE
#  include <sys/types.h>
#  include <sys/event.h>
#  include <sys/time.h>
#endif

#if !defined(HAVE_INVOCABLE_R) && defined(HAVE_INVOKABLE_R)
namespace std {
template <class _Ret, class _Fp, class ..._Args>
using is_invocable_r = __invokable_r<_Ret, _Fp, _Args...>;
}
#endif

namespace reckoning {
namespace event {

class EventLoop
{
public:
    EventLoop();
    ~EventLoop();

    template<typename T, typename ...Args>
    typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, void>::type
    send(T&& func, Args&& ...args);

    class Event
    {
    public:
        Event() { }
        virtual ~Event() { }

    protected:
        virtual void execute() = 0;

        friend class EventLoop;
    };

    template<typename T, typename std::enable_if<std::is_base_of<Event, T>::value, T>::type* = nullptr>
    void send(T&& event);
    void send(std::shared_ptr<Event>&& event);
    void post(std::shared_ptr<Event>&& event);

private:
    void init();
    void destroy();
    void wakeup();

private:
#ifdef HAVE_KQUEUE
    int mFd;
#endif
    std::thread::id mThread;
    std::mutex mMutex;
    std::vector<std::shared_ptr<Event> > mEvents;
};

namespace detail {
template<typename T, typename ...Args>
class PostEvent : public EventLoop::Event
{
public:
    PostEvent(T&& f, Args&& ...a) : func(std::forward<T>(f)), args(std::make_tuple(std::forward<Args>(a)...)) { }
    PostEvent(PostEvent<T, Args...>&& other) : func(std::move(other.func)), args(std::move(other.args)) { }
    ~PostEvent() override { }

    PostEvent<T, Args...>& operator=(PostEvent<T, Args...>&& other) { func = std::move(other.func); args = std::move(other.args); return *this; }

    virtual void execute() override { std::apply(func, args); }

private:
    PostEvent(const PostEvent&) = delete;
    PostEvent& operator=(const PostEvent&) = delete;

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
        std::shared_ptr<detail::PostEvent<T, Args...> > event = std::make_shared<detail::PostEvent<T, Args...> >(std::forward<T>(func), std::forward<Args>(args)...);
        post(std::move(event));
    }
}

template<typename T, typename std::enable_if<std::is_base_of<EventLoop::Event, T>::value, T>::type*>
inline void EventLoop::send(T&& event)
{
    if (mThread == std::this_thread::get_id()) {
        event.execute();
    } else {
        send(std::make_shared<Event>(new T(std::forward<Event>(event))));
    }
}

inline void EventLoop::send(std::shared_ptr<Event>&& event)
{
    if (mThread == std::this_thread::get_id()) {
        event->execute();
    } else {
        post(std::forward<std::shared_ptr<Event> >(event));
    }
}

inline void EventLoop::post(std::shared_ptr<Event>&& event)
{
    std::lock_guard<std::mutex> locker(mMutex);
    mEvents.push_back(std::forward<std::shared_ptr<Event> >(event));
    wakeup();
}

}} // namespace reckoning::event

#endif
