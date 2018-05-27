#include "EventLoop.h"

using namespace reckoning;
using namespace reckoning::event;

thread_local std::weak_ptr<EventLoop> EventLoop::tLoop;

EventLoop::EventLoop()
    : mStatus(0), mStopped(false)
{
    mThread = std::this_thread::get_id();
    //send([](int, const char*) -> void { }, 10, "123");
}

EventLoop::~EventLoop()
{
    destroy();
}

namespace reckoning {
namespace event {
std::shared_ptr<EventLoop> eventLoop()
{
    return EventLoop::loop();
}
}} // namespace reckoning::event
