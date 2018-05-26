#include "EventLoop.h"
#include "Timeval.h"
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <log/Log.h>

#define eintrwrap(VAR, BLOCK)                   \
    do {                                        \
        VAR = BLOCK;                            \
    } while (VAR == -1 && errno == EINTR)

using namespace reckoning;
using namespace reckoning::event;
using namespace reckoning::log;

void EventLoop::init()
{
    tLoop = shared_from_this();

    mFd = kqueue();
    if (mFd == -1) {
        Log(Log::Error) << "unable to open eventloop kqueue" << errno;
        cleanup();
        return;
    }
    mWakeup[0] = mWakeup[1] = -1;
    int e = pipe(mWakeup);
    if (e == -1) {
        Log(Log::Error) << "unable to make wakeup pope for eventloop" << errno;
        cleanup();
        return;
    }
#ifdef HAVE_NONBLOCK
    e = fcntl(mWakeup[0], F_GETFL, 0);
    if (e == -1) {
        Log(Log::Error) << "unable to get flags for wakeup pipe" << errno;
        cleanup();
        return;
    }
    e = fcntl(mWakeup[0], F_SETFL, e | O_NONBLOCK);
    if (e == -1) {
        Log(Log::Error) << "unable to set nonblock for wakeup pipe" << errno;
        cleanup();
        return;
    }
#endif
    struct kevent ev;
    memset(&ev, 0, sizeof(struct kevent));
    ev.ident = mWakeup[0];
    ev.flags = EV_ADD|EV_ENABLE;
    ev.filter = EVFILT_READ;
    eintrwrap(e, kevent(mFd, &ev, 1, 0, 0, 0));
}

void EventLoop::destroy()
{
    cleanup();
}

void EventLoop::wakeup()
{
    if (mThread == std::this_thread::get_id())
        return;
    int e;
    const int c = 'w';
    eintrwrap(e, write(mWakeup[1], &c, 1));
}

void EventLoop::cleanup()
{
    if (mFd != -1) {
        close(mFd);
        mFd = -1;
    }
    if (mWakeup[0] != -1) {
        close(mWakeup[0]);
        mWakeup[0] = -1;
    }
    if (mWakeup[1] != -1) {
        close(mWakeup[1]);
        mWakeup[1] = -1;
    }
}

int EventLoop::execute(std::chrono::milliseconds timeout)
{
    assert(tLoop.lock() != std::shared_ptr<EventLoop>());

    if (timeout != std::chrono::milliseconds{-1}) {
        timer(timeout, [this]() { exit(); });
    }

    auto resort = [](std::vector<std::shared_ptr<Timer> >& timers) {
        auto compare = [](const std::shared_ptr<Timer>& a, const std::shared_ptr<Timer>& b) -> bool {
            return a->mNext < b->mNext;
        };
        std::sort(timers.begin(), timers.end(), compare);
    };

    timespec ts;
    timespec* tsptr = nullptr;

    std::vector<std::unique_ptr<Event> > events;
    std::vector<std::shared_ptr<Timer> > timers;

    enum { MaxEvents = 64 };
    struct kevent kevents[MaxEvents];
    int e;
    for (;;) {
        // execute all events
        {
            std::lock_guard<std::mutex> locker(mMutex);
            // are we stopped?
            if (mStopped) {
                return mStatus;
            }

            events = std::move(mEvents);
        }
        for (const auto& e : events) {
            e->execute();
        }
        // when is our first timer?
        {
            std::lock_guard<std::mutex> locker(mMutex);
            if (!mTimers.empty()) {
                auto when = mTimers.front()->mNext - std::chrono::steady_clock::now();
                ts = std::chrono::duration_cast<timespec>(when);
                tsptr = &ts;
            } else {
                tsptr = nullptr;
            }
        }

        eintrwrap(e, kevent(mFd, 0, 0, kevents, MaxEvents, tsptr));
        if (e < 0) {
            // bad
            Log(Log::Error) << "unable to wait for events" << errno;
            cleanup();
            return -1;
        }

        for (int i = 0; i < e; ++i) {
            const int16_t filter = kevents[i].filter;
            const uint16_t flags = kevents[i].flags;
            const int fd = kevents[i].ident;
            if (flags & EV_ERROR) {
                // badness, we want this thing out
                struct kevent& kev = kevents[i];
                Log(Log::Warn) << "error on socket" << fd << kev.data;
                kev.flags = EV_DELETE|EV_DISABLE;
                kevent(mFd, &kev, 1, 0, 0, 0);
            } else {
                switch (filter) {
                case EVFILT_READ:
                    // read event
                    if (fd == mWakeup[0]) {
                        // read and discard
                        char c;
                        do {
                            e = read(fd, &c, 1);
                        } while (e == 1);
                    } else {
                    }
                    break;
                case EVFILT_WRITE:
                    // write event;
                    break;
                }
            }
        }

        // fire timers
        {
            auto now = std::chrono::steady_clock::now();

            std::lock_guard<std::mutex> locker(mMutex);
            auto it = mTimers.begin();
            auto end = mTimers.cend();
            while (it != end) {
                if ((*it)->mNext <= now) {
                    timers.push_back(*it);
                    if ((*it)->mFlag == Timeout) {
                        it = mTimers.erase(it);
                        end = mTimers.cend();
                    } else {
                        (*it)->mNext = now + (*it)->mTimeout;
                        ++it;
                    }
                } else {
                    break;
                }
            }
            resort(mTimers);
        }
        for (const auto& t : timers) {
            t->execute();
        }
        timers.clear();
    }
    return 0;
}

void EventLoop::exit(int status)
{
    std::lock_guard<std::mutex> locker(mMutex);
    mStopped = true;
    wakeup();
}
