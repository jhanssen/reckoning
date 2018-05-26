#include "EventLoop.h"

using namespace reckoning;
using namespace reckoning::event;

EventLoop::EventLoop()
    : mStatus(0), mStopped(false)
{
    mThread = std::this_thread::get_id();
    init();

    //send([](int, const char*) -> void { }, 10, "123");
}

EventLoop::~EventLoop()
{
    destroy();
}
