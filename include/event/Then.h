#ifndef THEN_H
#define THEN_H

#include <util/SpinLock.h>
#include <util/FunctionTraits.h>
#include <event/Loop.h>
#include <functional>
#include <type_traits>
#include <memory>
#include <string>
#include <optional>
#include <cassert>

namespace reckoning {
namespace event {

namespace detail {
struct ThenBase
{
};

struct MaybeFailBase
{
};

template<typename T>
inline constexpr bool isThen = std::is_base_of<ThenBase, typename std::decay<T>::type>::value;

template<typename T>
inline constexpr bool isMaybeFail = std::is_base_of<MaybeFailBase, typename std::decay<T>::type>::value;

template<typename T>
inline constexpr bool isVoid = std::is_void<typename std::decay<T>::type>::value;

template<typename T, typename U>
inline constexpr bool isSame = std::is_same<typename std::decay<T>::type, typename std::decay<U>::type>::value;

template<typename Traits>
inline constexpr bool hasAtleastOneArgument = Traits::arity > 0;

template<typename Traits, typename = void>
struct Arg0Type
{
    using type = void;
};

template<typename Traits>
struct Arg0Type<Traits, std::void_t<typename std::enable_if<hasAtleastOneArgument<Traits> >::type> >
{
    using type = typename Traits::template argument<0>::type;
};
} // namespace detail

class Fail
{
public:
    Fail(const std::string& err)
        : mError(err)
    {
    }

    Fail(std::string&& err)
        : mError(std::move(err))
    {
    }

    std::string&& error()
    {
        return std::move(mError);
    }

private:
    std::string mError;
};

template<typename T, typename = void>
class MaybeFail : public detail::MaybeFailBase
{
public:
    using ArgType = void;

    MaybeFail()
        : mSet(false), mFail(false)
    {
    }

    MaybeFail(Fail&& fail)
        : mError(std::move(fail.error())), mFail(true)
    {
    }

    bool isFail() const
    {
        return mFail;
    }

    std::string&& error()
    {
        return std::move(mError);
    }

private:
    std::string mError;
    bool mSet, mFail;
};

template<typename T>
class MaybeFail<T, std::void_t<typename std::enable_if<!std::is_void<T>::value>::type> > : public detail::MaybeFailBase
{
public:
    using ArgType = typename std::decay<T>::type;

    MaybeFail()
        : mSet(false), mFail(false)
    {
    }

    MaybeFail(T&& value)
        : mValue(std::forward<T>(value)), mSet(true), mFail(false)
    {
    }

    MaybeFail(Fail&& fail)
        : mError(std::move(fail.error())), mFail(true)
    {
    }

    bool isFail() const
    {
        return mFail;
    }

    std::string&& error()
    {
        return std::move(mError);
    }

    T&& value()
    {
        return std::move(mValue);
    }

private:
    T mValue;
    std::string mError;
    bool mSet, mFail;
};

// for ArgType == void
template<typename Arg, typename = void>
class Then : public detail::ThenBase
{
public:
    using ArgType = void;

    template<typename Functor>
    auto then(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, void>
                  && detail::isThen<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> typename util::function_traits<Functor>::return_type&
    {
        using Return = typename std::decay<typename util::function_traits<typename std::decay<Functor>::type>::return_type>::type;
        using ArgOfThen = typename Return::ArgType;
        std::shared_ptr<Return> chain = std::make_shared<Return>();
        auto loop = event::Loop::loop();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)]() mutable {
            //chain->resolve(func(std::forward<Arg>(arg)));
            auto& newchain = func();
            newchain.then([chain](ArgOfThen&& arg) {
                return chain->resolve(std::forward<ArgOfThen>(arg));
            });
            newchain.fail([chain](std::string&& failure) {
                chain->reject(std::move(failure));
            });
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        assert(!mFailed || !mResolved);
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mResolved) {
            loop->post([next = std::move(mNext)]() mutable {
                next();
            });
        } else {
            mLoop = loop;
        }
        return *chain.get();
    }

    template<typename Functor>
    auto then(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, void>
                  && detail::isMaybeFail<typename util::function_traits<Functor>::return_type>
                  && !detail::isVoid<typename util::function_traits<Functor>::return_type::ArgType>, int
              > = 0) -> Then<typename util::function_traits<Functor>::return_type::ArgType>&
    {
        using Return = typename util::function_traits<Functor>::return_type;
        std::shared_ptr<Then<typename Return::ArgType> > chain = std::make_shared<Then<typename Return::ArgType> >();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)]() mutable {
            auto maybeFail = func();
            if (maybeFail.isFail()) {
                chain->reject(std::move(maybeFail.error()));
            } else {
                chain->resolve(std::move(maybeFail.value()));
            }
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        auto loop = event::Loop::loop();
        assert(!mFailed || !mResolved);
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mResolved) {
            loop->post([next = std::move(mNext)]() mutable {
                next();
            });
        } else {
            mLoop = loop;
        }
        return *chain.get();
    }

    template<typename Functor>
    auto then(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, void>
                  && detail::isMaybeFail<typename util::function_traits<Functor>::return_type>
                  && detail::isVoid<typename util::function_traits<Functor>::return_type::ArgType>, int
              > = 0) -> Then<typename util::function_traits<Functor>::return_type::ArgType>&
    {
        std::shared_ptr<Then<void> > chain = std::make_shared<Then<void> >();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)]() mutable {
            auto maybeFail = func();
            if (maybeFail.isFail()) {
                chain->reject(std::move(maybeFail.error()));
            } else {
                chain->resolve();
            }
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        auto loop = event::Loop::loop();
        assert(!mFailed || !mResolved);
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mResolved) {
            loop->post([next = std::move(mNext)]() mutable {
                next();
            });
        } else {
            mLoop = loop;
        }
        return *chain.get();
    }

    template<typename Functor>
    auto then(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, void>
                  && !detail::isThen<typename util::function_traits<Functor>::return_type>
                  && !detail::isMaybeFail<typename util::function_traits<Functor>::return_type>
                  && !detail::isVoid<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> Then<typename util::function_traits<Functor>::return_type>&
    {
        using Return = typename std::decay<typename util::function_traits<typename std::decay<Functor>::type>::return_type>::type;
        std::shared_ptr<Then<Return> > chain = std::make_shared<Then<Return> >();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)]() mutable {
            chain->resolve(func());
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        auto loop = event::Loop::loop();
        assert(!mFailed || !mResolved);
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mResolved) {
            loop->post([next = std::move(mNext)]() mutable {
                next();
            });
        } else {
            mLoop = loop;
        }
        return *chain.get();
    }

    template<typename Functor>
    auto then(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, void>
                  && detail::isVoid<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> Then<void>&
    {
        std::shared_ptr<Then<void> > chain = std::make_shared<Then<void> >();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)]() mutable {
            func();
            chain->resolve();
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        auto loop = event::Loop::loop();
        assert(!mFailed || !mResolved);
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mResolved) {
            loop->post([next = std::move(mNext)]() mutable {
                next();
            });
        } else {
            mLoop = loop;
        }
        return *chain.get();
    }

    template<typename Functor>
    auto fail(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, std::string>
                  && detail::isVoid<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> void
    {
        util::SpinLocker locker(mLock);
        mFail = [func = std::move(func)](std::string&& arg) mutable {
            func(std::move(arg));
        };
        auto loop = event::Loop::loop();
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        } else {
            mLoop = loop;
        }
    }

    void resolve()
    {
        std::function<void()> next;
        std::shared_ptr<event::Loop> loop;
        {
            util::SpinLocker locker(mLock);
            next = std::move(mNext);
            if (next) {
                loop = mLoop.lock();
            } else {
                mResolved = true;
                return;
            }
        }
        assert(next);
        if (loop) {
            loop->send([next = std::move(next)]() mutable {
                next();
            });
        } else {
            next();
        }
    }

    void reject(std::string&& failure)
    {
        std::function<void(std::string&&)> fail;
        std::shared_ptr<event::Loop> loop;
        {
            util::SpinLocker locker(mLock);
            fail = std::move(mFail);
            if (fail) {
                loop = mLoop.lock();
            } else {
                mFailed = true;
                mFailure = std::move(failure);
                return;
            }
        }
        assert(fail);
        if (loop) {
            loop->send([fail = std::move(fail), failure = std::move(failure)]() mutable {
                fail(std::move(failure));
            });
        } else {
            fail(std::move(failure));
        }
    }

private:
    util::SpinLock mLock {};
    std::function<void()> mNext;
    std::function<void(std::string&&)> mFail;
    std::weak_ptr<event::Loop> mLoop;
    std::string mFailure;
    bool mResolved { false }, mFailed { false };
};

// for ArgType != void
template<typename Arg>
class Then<Arg, std::void_t<typename std::enable_if<!std::is_void<Arg>::value>::type> > : public detail::ThenBase
{
public:
    using ArgType = typename std::decay<Arg>::type;

    template<typename Functor>
    auto then(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, Arg>
                  && detail::isThen<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> typename util::function_traits<Functor>::return_type&
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
            newchain.fail([chain](std::string&& failure) {
                chain->reject(std::move(failure));
            });
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        assert(!mFailed || !mArg.has_value());
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mArg.has_value()) {
            loop->post([next = std::move(mNext), arg = std::move(mArg.value())]() mutable {
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
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, Arg>
                  && detail::isMaybeFail<typename util::function_traits<Functor>::return_type>
                  && !detail::isVoid<typename util::function_traits<Functor>::return_type::ArgType>, int
              > = 0) -> Then<typename util::function_traits<Functor>::return_type::ArgType>&
    {
        using Return = typename util::function_traits<Functor>::return_type;
        std::shared_ptr<Then<typename Return::ArgType> > chain = std::make_shared<Then<typename Return::ArgType> >();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)](Arg&& arg) mutable {
            auto maybeFail = func(std::forward<Arg>(arg));
            if (maybeFail.isFail()) {
                chain->reject(std::move(maybeFail.error()));
            } else {
                chain->resolve(std::move(maybeFail.value()));
            }
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        auto loop = event::Loop::loop();
        assert(!mFailed || !mArg.has_value());
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mArg.has_value()) {
            loop->post([next = std::move(mNext), arg = std::move(mArg.value())]() mutable {
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
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, Arg>
                  && detail::isMaybeFail<typename util::function_traits<Functor>::return_type>
                  && detail::isVoid<typename util::function_traits<Functor>::return_type::ArgType>, int
              > = 0) -> Then<typename util::function_traits<Functor>::return_type::ArgType>&
    {
        std::shared_ptr<Then<void> > chain = std::make_shared<Then<void> >();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)](Arg&& arg) mutable {
            auto maybeFail = func(std::forward<Arg>(arg));
            if (maybeFail.isFail()) {
                chain->reject(std::move(maybeFail.error()));
            } else {
                chain->resolve();
            }
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        auto loop = event::Loop::loop();
        assert(!mFailed || !mArg.has_value());
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mArg.has_value()) {
            loop->post([next = std::move(mNext), arg = std::move(mArg.value())]() mutable {
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
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, Arg>
                  && !detail::isThen<typename util::function_traits<Functor>::return_type>
                  && !detail::isMaybeFail<typename util::function_traits<Functor>::return_type>
                  && !detail::isVoid<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> Then<typename util::function_traits<Functor>::return_type>&
    {
        using Return = typename std::decay<typename util::function_traits<typename std::decay<Functor>::type>::return_type>::type;
        std::shared_ptr<Then<Return> > chain = std::make_shared<Then<Return> >();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)](Arg&& arg) mutable {
            chain->resolve(func(std::forward<Arg>(arg)));
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        auto loop = event::Loop::loop();
        assert(!mFailed || !mArg.has_value());
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mArg.has_value()) {
            loop->post([next = std::move(mNext), arg = std::move(mArg.value())]() mutable {
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
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, Arg>
                  && detail::isVoid<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> Then<void>&
    {
        std::shared_ptr<Then<void> > chain = std::make_shared<Then<void> >();
        util::SpinLocker locker(mLock);
        mNext = [chain, func = std::move(func)](Arg&& arg) mutable {
            func(std::forward<Arg>(arg));
            chain->resolve();
        };
        mFail = [chain](std::string&& failure) {
            chain->reject(std::move(failure));
        };
        auto loop = event::Loop::loop();
        assert(!mFailed || !mArg.has_value());
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
            });
        }
        if (mArg.has_value()) {
            loop->post([next = std::move(mNext), arg = std::move(mArg.value())]() mutable {
                next(std::move(arg));
            });
        } else {
            mLoop = loop;
        }
        return *chain.get();
    }

    template<typename Functor>
    auto fail(Functor&& func,
              typename std::enable_if_t<
                  detail::isSame<typename detail::Arg0Type<typename util::function_traits<Functor> >::type, std::string>
                  && detail::isVoid<typename util::function_traits<Functor>::return_type>, int
              > = 0) -> void
    {
        util::SpinLocker locker(mLock);
        mFail = [func = std::move(func)](std::string&& arg) mutable {
            func(std::move(arg));
        };
        auto loop = event::Loop::loop();
        assert(!mFailed || !mArg.has_value());
        if (mFailed) {
            loop->post([fail = std::move(mFail), failure = std::move(mFailure)]() mutable {
                fail(std::move(failure));
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

    void reject(std::string&& failure)
    {
        std::function<void(std::string&&)> fail;
        std::shared_ptr<event::Loop> loop;
        {
            util::SpinLocker locker(mLock);
            fail = std::move(mFail);
            if (fail) {
                loop = mLoop.lock();
            } else {
                mFailed = true;
                mFailure = std::move(failure);
                return;
            }
        }
        assert(fail);
        if (loop) {
            loop->send([fail = std::move(fail), failure = std::move(failure)]() mutable {
                fail(std::move(failure));
            });
        } else {
            fail(std::move(failure));
        }
    }

private:
    util::SpinLock mLock {};
    std::function<void(Arg&&)> mNext;
    std::function<void(std::string&&)> mFail;
    std::optional<Arg> mArg;
    std::weak_ptr<event::Loop> mLoop;
    std::string mFailure;
    bool mFailed { false };

};

}} // namespace reckoning::event

#endif // THEN_H
