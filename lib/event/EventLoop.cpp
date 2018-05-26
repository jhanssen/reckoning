#include "EventLoop.h"

using namespace reckoning;
using namespace reckoning::event;

EventLoop::EventLoop()
{
    init();
}

EventLoop::~EventLoop()
{
    destroy();
}
