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
    tLoop = shared_from_this();

    mFd = kqueue();
    if (mFd == -1) {
        Log(Log::Error) << "unable to open eventloop kqueue" << errno;
        cleanup();
        return;
    }

    commonInit();

    struct kevent ev;
    memset(&ev, 0, sizeof(struct kevent));
    ev.ident = mWakeup[0];
    ev.flags = EV_ADD|EV_ENABLE;
    ev.filter = EVFILT_READ;
    eintrwrap(e, kevent(mFd, &ev, 1, 0, 0, 0));
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

    auto processFd = [this](int fd, uint8_t flags)
    {
        std::unique_lock<std::mutex> locker(mMutex);
        auto it = mFds.begin();
        const auto end = mFds.end();
        while (it != end) {
            if (it->first == fd) {
                if (flags & FdError) {
                    // let it go
                    mFds.erase(it);
                }

                auto func = it->second;
                locker.unlock();
                func(fd, flags);
                return;
            }
            ++it;
        }
    };

    timespec ts;
    timespec* tsptr = nullptr;

    std::vector<std::unique_ptr<Event> > events;
    std::vector<std::shared_ptr<Timer> > timers;
    std::vector<int> fds;

    enum { MaxEvents = 64 };
    struct kevent kevents[MaxEvents];
    int e;
    for (;;) {
        // execute all events
        {
            std::lock_guard<std::mutex> locker(mMutex);
            // are we stopped?
            if (mStopped) {
                // shutdown threads etc
                net::Resolver::resolver().shutdown();

                return mStatus;
            }

            events = std::move(mEvents);

        }
        for (const auto& e : events) {
            e->execute();
        }

        // process new fds
        {
            std::lock_guard<std::mutex> locker(mMutex);
            if (!mPendingFds.empty()) {
                fds.clear();
                fds.reserve(mPendingFds.size());
                for (const auto &fd : mPendingFds) {
                    fds.push_back(fd.first);
                }
                mFds.insert(mFds.end(), std::make_move_iterator(mPendingFds.begin()), std::make_move_iterator(mPendingFds.end()));
                mPendingFds.clear();
            }
        }
        // add new fds
        if (!fds.empty()) {
            struct kevent ev;
            memset(&ev, 0, sizeof(struct kevent));
            for (auto fd : fds) {
                // printf("adding fd %d\n", fd);
                ev.ident = fd;
                ev.flags = EV_ADD|EV_ENABLE;
                ev.filter = EVFILT_READ;
                eintrwrap(e, kevent(mFd, &ev, 1, 0, 0, 0));
            }
            fds.clear();
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

            // also, while we have the mutex, process removed fds
            fds = std::move(mRemovedFds);

            // and update sockets, hopefully we won't have too many of these
            if (!mUpdateFds.empty()) {
                struct kevent ev;
                memset(&ev, 0, sizeof(struct kevent));
                for (auto update : mUpdateFds) {
                    ev.ident = update.first;
                    // printf("updating fd %ld with flag %d\n", ev.ident, update.second);
                    if (update.second & FdRead) {
                        // printf("add read\n");
                        ev.flags = EV_ADD|EV_ENABLE;
                        ev.filter = EVFILT_READ;
                        eintrwrap(e, kevent(mFd, &ev, 1, 0, 0, 0));
                    } else {
                        // printf("remove read\n");
                        ev.flags = EV_DELETE|EV_DISABLE;
                        ev.filter = EVFILT_READ;
                        eintrwrap(e, kevent(mFd, &ev, 1, 0, 0, 0));
                    }
                    if (update.second & FdWrite) {
                        // printf("add write\n");
                        ev.flags = EV_ADD|EV_ENABLE;
                        ev.filter = EVFILT_WRITE;
                        eintrwrap(e, kevent(mFd, &ev, 1, 0, 0, 0));
                    } else {
                        // printf("remove write\n");
                        ev.flags = EV_DELETE|EV_DISABLE;
                        ev.filter = EVFILT_WRITE;
                        eintrwrap(e, kevent(mFd, &ev, 1, 0, 0, 0));
                    }
                }
                mUpdateFds.clear();
            }
        }
        // remove fds
        if (!fds.empty()) {
            struct kevent ev;
            memset(&ev, 0, sizeof(struct kevent));
            for (auto fd : fds) {
                // printf("removing fd %d\n", fd);
                ev.ident = fd;
                ev.flags = EV_DELETE|EV_DISABLE;
                eintrwrap(e, kevent(mFd, &ev, 1, 0, 0, 0));
                eintrwrap(e, close(fd));
            }
            fds.clear();
        }

        eintrwrap(e, kevent(mFd, 0, 0, kevents, MaxEvents, tsptr));
        if (e < 0) {
            // bad
            Log(Log::Error) << "unable to wait for events" << errno;
            cleanup();
            return -1;
        }

        // printf("got %d events\n", e);

        for (int i = 0; i < e; ++i) {
            const int16_t filter = kevents[i].filter;
            const uint16_t flags = kevents[i].flags;
            const int fd = kevents[i].ident;

            // printf("event on fd %d\n", fd);

            if (flags & EV_ERROR) {
                // badness, we want this thing out
                struct kevent& kev = kevents[i];
                Log(Log::Warn) << "error on socket" << fd << kev.data;
                kev.flags = EV_DELETE|EV_DISABLE;
                kevent(mFd, &kev, 1, 0, 0, 0);

                processFd(fd, FdError);
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
                        processFd(fd, FdRead);
                    }
                    break;
                case EVFILT_WRITE:
                    // write event;
                    processFd(fd, FdWrite);
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
