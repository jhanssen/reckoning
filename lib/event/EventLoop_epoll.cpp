#include "EventLoop.h"
#include "Timeval.h"
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <log/Log.h>
#include <util/Socket.h>
#include <net/Resolver.h>

using namespace reckoning;
using namespace reckoning::event;
using namespace reckoning::log;

void EventLoop::init()
{
}

void EventLoop::destroy()
{
}

void EventLoop::wakeup()
{
}

void EventLoop::cleanup()
{
}

int EventLoop::execute(std::chrono::milliseconds timeout)
{
}

void EventLoop::exit(int status)
{
}
