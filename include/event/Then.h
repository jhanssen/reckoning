#ifndef THEN_H
#define THEN_H

#include <util/SpinLock.h>
#include <util/FunctionTraits.h>
#include <event/Loop.h>
#include <functional>
#include <type_traits>
#include <memory>
#include <optional>
#include <cassert>

namespace reckoning {
namespace event {

namespace detail {
struct ThenBase
{
};

template<typename T>
inline constexpr bool isThen = std::is_base_of<ThenBase, typename std::decay<T>::type>::value;

template<typename T>
inline constexpr bool isVoid = std::is_void<typename std::decay<T>::type>::value;

template<typename T, typename U>
inline constexpr bool isSame = std::is_same<typename std::decay<T>::type, typename std::decay<U>::type>::value;
} // namespace detail

template<typename Arg>
class Then : public detail::ThenBase
{
public:
    using ArgType = typename std::decay<Arg>::type;

    template<typename Functor>
    auto then(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename util::function_traits<Functor>::arg0_type, Arg>
                  && detail::isThen<typename util::function_traits<Functor>::return_type
              >, int> = 0) -> typename util::function_traits<Functor>::return_type&
    {
        using Return = typename std::decay<typename util::function_traits<typename std::decay<Functor>::type>::return_type>::type;
        using ArgOfThen = typename Return::ArgType;
        std::shared_ptr<Return> chain = std::make_shared<Return>();
        auto loop = event::Loop::loop();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)](Arg&& arg) mutable {
            //chain->resolve(func(std::forward<Arg>(arg)));
            auto& newchain = func(std::forward<Arg>(arg));
            newchain.then([chain](ArgOfThen&& arg) {
                return chain->resolve(std::forward<ArgOfThen>(arg));
            });
        };
        if (mArg.has_value()) {
            loop->send([next = std::move(mNext), arg = std::move(mArg.value())]() mutable {
                next(std::move(arg));
            });
        } else {
            mLoop = loop;
        }
        return *chain.get();
    }

    template<typename Functor>
    auto then(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename util::function_traits<Functor>::arg0_type, Arg>
                  && !detail::isThen<typename util::function_traits<Functor>::return_type>
                  && !detail::isVoid<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> Then<typename util::function_traits<Functor>::return_type>&
    {
        using Return = typename std::decay<typename util::function_traits<typename std::decay<Functor>::type>::return_type>::type;
        std::shared_ptr<Then<Return> > chain = std::make_shared<Then<Return> >();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)](Arg&& arg) mutable {
            chain->resolve(func(std::forward<Arg>(arg)));
        };
        auto loop = event::Loop::loop();
        if (mArg.has_value()) {
            loop->send([next = std::move(mNext), arg = std::move(mArg.value())]() mutable {
                next(std::move(arg));
            });
        } else {
            mLoop = loop;
        }
        return *chain.get();
    }

    template<typename Functor>
    auto then(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename util::function_traits<Functor>::arg0_type, Arg>
                  && detail::isVoid<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> void
    {
        util::SpinLocker locker(mLock);
        mNext = [func = std::move(func)](Arg&& arg) mutable {
            func(std::forward<Arg>(arg));
        };
        auto loop = event::Loop::loop();
        if (mArg.has_value()) {
            loop->send([next = std::move(mNext), arg = std::move(mArg.value())]() mutable {
                next(std::move(arg));
            });
        } else {
            mLoop = loop;
        }
    }

    void resolve(Arg&& arg)
    {
        std::function<void(Arg&&)> next;
        std::shared_ptr<event::Loop> loop;
        {
            util::SpinLocker locker(mLock);
            next = std::move(mNext);
            if (next) {
                loop = mLoop.lock();
            } else {
                mArg = std::forward<Arg>(arg);
                return;
            }
        }
        assert(next);
        if (loop) {
            loop->send([next = std::move(next), arg = std::forward<Arg>(arg)]() mutable {
                next(std::forward<Arg>(arg));
            });
        } else {
            next(std::forward<Arg>(arg));
        }
    }

private:
    util::SpinLock mLock {};
    std::function<void(Arg&&)> mNext;
    std::optional<Arg> mArg;
    std::weak_ptr<event::Loop> mLoop;
};

}} // namespace reckoning::event

#endif // THEN_H
