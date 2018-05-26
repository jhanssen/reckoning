#include "EventLoop.h"
#include <unistd.h>

using namespace reckoning;
using namespace reckoning::event;

void EventLoop::init()
{
    mFd = kqueue();
}

void EventLoop::destroy()
{
    close(mFd);
}
