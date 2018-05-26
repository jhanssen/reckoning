#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <atomic>
#include <mutex>
#include <cassert>

namespace reckoning {
namespace util {

class SpinLock
{
public:
    void lock()
    {
        while (mAtomic.test_and_set(std::memory_order_acquire)) { }
    }
    void unlock()
    {
        mAtomic.clear(std::memory_order_release);
    }

private:
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

}} // namespace reckoning::event

#endif
