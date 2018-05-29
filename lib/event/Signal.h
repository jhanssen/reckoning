#ifndef SIGNAL_H
#define SIGNAL_H

#include <config.h>
#include <util/SpinLock.h>
#include <event/EventLoop.h>
#include <cassert>
#include <memory>
#include <functional>

#if !defined(HAVE_INVOCABLE_R) && defined(HAVE_INVOKABLE_R)
namespace std {
template <class _Ret, class _Fp, class ..._Args>
using is_invocable_r = __invokable_r<_Ret, _Fp, _Args...>;
}
#endif

namespace reckoning {
namespace event {

template<typename ...Args>
class Signal;

namespace detail {
template<typename ...Args>
class ConnectionBase
{
public:
    ConnectionBase();

    void invoke(Args&& ...args);
    void disconnect();
    bool connected() const;

private:
    std::function<void(typename std::decay<Args>::type...)> mFunction;
    std::weak_ptr<EventLoop> mLoop;
    std::atomic<bool> mConnected;

    friend class Signal<Args...>;
};
} // namespace detail

template<typename ...Args>
class Signal
{
public:
    Signal();
    Signal(Signal&& other);
    ~Signal();

    Signal& operator=(Signal&& other);

    class Connection
    {
    public:
        Connection();

        void disconnect();
        bool connected() const;

    private:
        Connection(const std::shared_ptr<detail::ConnectionBase<Args...> >& connection);

        std::weak_ptr<detail::ConnectionBase<Args...> > mConnection;

        friend class Signal;
    };

    template<typename T>
    typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, Connection>::type
    connect(T&& func);
    void disconnect();
    void emit(Args&& ...args);

private:
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    std::vector<std::shared_ptr<detail::ConnectionBase<Args...> > > mConnections;
    util::SpinLock mLock;
};

namespace detail {
template<typename ...Args>
ConnectionBase<Args...>::ConnectionBase()
    : mConnected(true)
{
}

template<typename ...Args>
void ConnectionBase<Args...>::invoke(Args&& ...args)
{
    auto loop = mLoop.lock();
    if (loop) {
        loop->send(mFunction, std::forward<Args>(args)...);
    } else {
        mFunction(std::forward<Args>(args)...);
    }
}

template<typename ...Args>
void ConnectionBase<Args...>::disconnect()
{
    mConnected.exchange(false);
}

template<typename ...Args>
bool ConnectionBase<Args...>::connected() const
{
    return mConnected;
}
} // namespace detail

template<typename ...Args>
Signal<Args...>::Connection::Connection()
{
}

template<typename ...Args>
inline Signal<Args...>::Connection::Connection(const std::shared_ptr<detail::ConnectionBase<Args...> >& connection)
    : mConnection(connection)
{
}

template<typename ...Args>
inline void Signal<Args...>::Connection::disconnect()
{
    auto base = mConnection.lock();
    if (base) {
        base->disconnect();
    }
}

template<typename ...Args>
inline bool Signal<Args...>::Connection::connected() const
{
    auto base = mConnection.lock();
    if (base) {
        return base->connected();
    }
    return false;
}

template<typename ...Args>
inline Signal<Args...>::Signal()
{
}

template<typename ...Args>
inline Signal<Args...>::Signal(Signal&& other)
    : mConnections(std::move(other.mConnections)), mLock(std::move(other.mLock))
{
}

template<typename ...Args>
inline Signal<Args...>::~Signal()
{
}

template<typename ...Args>
inline Signal<Args...>& Signal<Args...>::operator=(Signal&& other)
{
    mConnections = std::move(other.mConnections);
    mLock = std::move(other.mLock);
    return *this;
}

template<typename ...Args>
inline void Signal<Args...>::disconnect()
{
    util::SpinLocker locker(mLock);
    mConnections.clear();
}

template<typename ...Args>
inline void Signal<Args...>::emit(Args&& ...args)
{
    std::vector<std::shared_ptr<detail::ConnectionBase<Args...> > > connections;
    {
        util::SpinLocker locker(mLock);
        auto it = mConnections.begin();
        auto end = mConnections.cend();
        while (it != end) {
            if ((*it)->connected()) {
                connections.push_back(*it);
                ++it;
            } else {
                it = mConnections.erase(it);
                end = mConnections.cend();
            }
        }
    }
    for (const auto& conn : connections) {
        if (!conn->connected())
            continue;
        // there's a race here, the connection can get disconnected between the time
        // we asked if it was connected above and to here where we actually invoke.
        // but we can live with that.
        conn->invoke(std::forward<Args>(args)...);
    }
}

template<typename ...Args>
template<typename T>
inline typename std::enable_if<std::is_invocable_r<void, T, Args...>::value, typename Signal<Args...>::Connection>::type
Signal<Args...>::connect(T&& func)
{
    util::SpinLocker locker(mLock);

    auto base = std::make_shared<detail::ConnectionBase<Args...> >();
    base->mLoop = EventLoop::loop();
    base->mFunction = std::forward<T>(func);
    mConnections.push_back(base);
    return Connection(base);
}

}} // namespace reckoning::event

#endif
