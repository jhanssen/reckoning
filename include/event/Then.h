#ifndef THEN_H
#define THEN_H

#include <util/SpinLock.h>
#include <event/Loop.h>
#include <functional>
#include <memory>
#include <optional>

namespace reckoning {
namespace event {

template<typename T>
class Then
{
public:
    Then() { }

    void then(std::function<void(T&&)>&& functor);
    void resolve(T&& value);

    void clear();

private:
    std::optional<T> mValue {};
    std::function<void(T&&)> mFunctor {};

    util::SpinLock mLock {};
    std::weak_ptr<event::Loop> mLoop {};

private:
    Then(const Then&) = delete;
    Then(Then&& other) = delete;
    Then& operator=(const Then&) = delete;
    Then& operator=(Then&&) = delete;
};

template<typename T>
void Then<T>::then(std::function<void(T&&)>&& functor)
{
    util::SpinLocker locker(mLock);
    if (mValue.has_value()) {
        functor(std::move(mValue.value()));
    } else {
        mLoop = Loop::loop();
        mFunctor = std::move(functor);
    }
}

template<typename T>
void Then<T>::resolve(T&& value)
{
    util::SpinLocker locker(mLock);
    if (mFunctor) {
        auto loop = mLoop.lock();
        if (loop) {
            loop->send(std::move(mFunctor), std::forward<T>(value));
        } else {
            mFunctor(std::forward<T>(value));
        }
    } else {
        mValue = std::forward<T>(value);
    }
}

template<typename T>
void Then<T>::clear()
{
    mValue = {};
    mFunctor = {};
    mLoop = {};
}

}} // namespace reckoning::event

#endif
