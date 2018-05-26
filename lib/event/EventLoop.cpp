#include "EventLoop.h"

using namespace reckoning;
using namespace reckoning::event;

EventLoop::EventLoop()
{
    mThread = std::this_thread::get_id();
    init();

    //send([](int, const char*) -> void { }, 10, "123");
}

EventLoop::~EventLoop()
{
    destroy();
}
