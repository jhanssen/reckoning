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
    post(T&& func, Args&& ...args);

    class Event
    {
    public:
        Event() { }
        virtual ~Event() { }

    protected:
        virtual void execute() = 0;

        friend class EventLoop;
    };

    void post(Event&& event);
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
    ~PostEvent() override { }

    virtual void execute() override { std::apply(func, args); }

private:
    std::function<void(typename std::decay<Args>::type...)> func;
    std::tuple<typename std::decay<Args>::type...> args;
};
} // namespace detail

template<typename T, typename ...Args>
inline typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, void>::type
EventLoop::post(T&& func, Args&& ...args)
{
    if (mThread == std::this_thread::get_id()) {
        func(std::forward<Args>(args)...);
    } else {
        std::shared_ptr<detail::PostEvent<T, Args...> > event = std::make_shared<detail::PostEvent<T, Args...> >(std::forward<T>(func), std::forward<Args>(args)...);
        post(std::move(event));
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
