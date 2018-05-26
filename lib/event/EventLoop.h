#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <config.h>

#ifdef HAVE_KQUEUE
#  include <sys/types.h>
#  include <sys/event.h>
#  include <sys/time.h>
#endif

namespace reckoning {
namespace event {

class EventLoop
{
public:
    EventLoop();
    ~EventLoop();

private:
    void init();
    void destroy();

private:
#ifdef HAVE_KQUEUE
    int mFd;
#endif
};

}} // namespace reckoning::event

#endif
