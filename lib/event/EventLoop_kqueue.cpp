#include "EventLoop.h"
#include <unistd.h>
#include <errno.h>
#include <log/Log.h>

using namespace reckoning;
using namespace reckoning::event;
using namespace reckoning::log;

void EventLoop::init()
{
    mFd = kqueue();
    if (mFd == -1) {
        Log(Log::Error) << "unable to open eventloop kqueue" << errno;
    }
}

void EventLoop::destroy()
{
    close(mFd);
}
