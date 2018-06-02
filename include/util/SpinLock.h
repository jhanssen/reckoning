#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <atomic>
#include <cassert>

namespace reckoning {
namespace util {

class SpinLock
{
public:
    SpinLock();
    SpinLock(SpinLock&& other);
    SpinLock& operator=(SpinLock&& other);

    void lock()
    {
        while (mAtomic.test_and_set(std::memory_order_acquire)) { }
    }
    void unlock()
    {
        mAtomic.clear(std::memory_order_release);
    }

private:
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    std::atomic_flag mAtomic = ATOMIC_FLAG_INIT;
};

class SpinLocker
{
public:
    SpinLocker(SpinLock& lock)
        : mLock(lock)
    {
        mLock.lock();
    }
    ~SpinLocker()
    {
        mLock.unlock();
    }

private:
    SpinLock& mLock;
};

inline SpinLock::SpinLock()
{
}

inline SpinLock::SpinLock(SpinLock&& other)
{
    // assert that we don't hold the lock
    assert(!other.mAtomic.test_and_set(std::memory_order_acquire));
}

inline SpinLock& SpinLock::operator=(SpinLock&& other)
{
    // assert that we don't hold the lock
    assert(!mAtomic.test_and_set(std::memory_order_acquire));
#ifndef NDEBUG
    mAtomic.clear(std::memory_order_release);
#endif
    assert(!other.mAtomic.test_and_set(std::memory_order_acquire));
    return *this;
}

}} // namespace reckoning::util

#endif
