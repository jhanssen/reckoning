#include "Loop.h"
#include <util/Socket.h>
#include <log/Log.h>
#include <fcntl.h>
#include <unistd.h>

using namespace reckoning;
using namespace reckoning::event;
using namespace reckoning::log;

thread_local std::weak_ptr<Loop> Loop::tLoop;

Loop::Loop()
    : mStatus(0), mStopped(false)
{
    mThread = std::this_thread::get_id();
    //send([](int, const char*) -> void { }, 10, "123");
}

Loop::~Loop()
{
    destroy();
}

void Loop::commonInit()
{
    mWakeup[0] = mWakeup[1] = -1;
    int e = pipe(mWakeup);
    if (e == -1) {
        Log(Log::Error) << "unable to make wakeup pope for eventloop" << errno;
        cleanup();
        return;
    }
#ifdef HAVE_NONBLOCK
    if (!util::socket::setFlag(mWakeup[0], O_NONBLOCK)) {
        Log(Log::Error) << "unable to set nonblock for wakeup pipe" << errno;
        cleanup();
        return;
    }
#endif
}

void Loop::destroy()
{
    cleanup();
}

void Loop::wakeup()
{
    if (mThread == std::this_thread::get_id())
        return;
    int e;
    const int c = 'w';
    eintrwrap(e, write(mWakeup[1], &c, 1));
}

void Loop::cleanup()
{
    int e;
    if (mFd != -1) {
        eintrwrap(e, close(mFd));
        mFd = -1;
    }
    if (mWakeup[0] != -1) {
        eintrwrap(e, close(mWakeup[0]));
        mWakeup[0] = -1;
    }
    if (mWakeup[1] != -1) {
        eintrwrap(e, close(mWakeup[1]));
        mWakeup[1] = -1;
    }
}

void Loop::exit(int status)
{
    std::lock_guard<std::mutex> locker(mMutex);
    mStopped = true;
    wakeup();
}
