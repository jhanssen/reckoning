#include <event/Loop.h>
#include <event/Timeval.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <log/Log.h>
#include <util/Socket.h>
#include <net/Resolver.h>

using namespace reckoning;
using namespace reckoning::event;
using namespace reckoning::log;

void Loop::init()
{
    tLoop = shared_from_this();

    mFd = kqueue();
    if (mFd == -1) {
        Log(Log::Error) << "unable to open eventloop kqueue" << errno;
        cleanup();
        return;
    }

    commonInit();

    int e;
    struct kevent ev;
    memset(&ev, 0, sizeof(struct kevent));
    ev.ident = mWakeup[0];
    ev.flags = EV_ADD|EV_ENABLE;
    ev.filter = EVFILT_READ;
    eintrwrap(e, kevent(mFd, &ev, 1, 0, 0, 0));
    if (e == -1) {
        Log(Log::Error) << "unable to add wakeup pipe to kqueue" << errno;
        cleanup();
        return;
    }
}

void Loop::deinit()
{
    net::Resolver::resolver().shutdown();
}

int Loop::execute(std::chrono::milliseconds timeout)
{
    assert(tLoop.lock() != std::shared_ptr<Loop>());

    int executeTimeout;
    bool hasTimeout;
    if (timeout != std::chrono::milliseconds{-1}) {
        //addTimer(timeout, [this]() { exit(); });
        executeTimeout = timeout.count();
        hasTimeout = true;
    } else {
        executeTimeout = std::numeric_limits<int>::max();
        hasTimeout = false;
    }
    int64_t startTime = timeNow();

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

    timespec ts, ets;
    timespec* tsptr = nullptr;

    std::vector<std::unique_ptr<Event> > events;
    std::vector<std::shared_ptr<Timer> > timers;
    std::vector<int> fds;

    enum { MaxEvents = 64 };
    struct kevent kevents[MaxEvents];
    int e;
    for (;;) {
        // execute all events
        for (;;) {
            {
                // are we stopped?
                if (mStopped.load(std::memory_order_acquire)) {
                    return mStatus;
                }

                std::lock_guard<std::mutex> locker(mMutex);
                events = std::move(mEvents);
                if (events.empty())
                    break;
            }
            for (const auto& e : events) {
                e->execute();
            }
        }

        // process new fds
        {
            // did one of the events stop us?
            if (mStopped.load(std::memory_order_acquire)) {
                return mStatus;
            }

            std::lock_guard<std::mutex> locker(mMutex);
            if (!mPendingFds.empty()) {
                fds.clear();
                fds.reserve(mPendingFds.size());
                for (const auto &fd : mPendingFds) {
                    fds.push_back(fd.first);
                }
                // these might be duplicated in mFds (in case someone removes and readds before the event loop has a time to process)
                // so we need to remove them if they do
                auto fit = mFds.begin();
                while (fit != mFds.end()) {
                    for (const auto& pending : mPendingFds) {
                        if (pending.first == fit->first) {
                            // remove it
                            fit = mFds.erase(fit);
                        } else {
                            ++fit;
                        }
                    }
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
            if (hasTimeout) {
                ets.tv_sec = executeTimeout / 1000;
                ets.tv_nsec = (executeTimeout % 1000) * 1000000;
            }

            std::lock_guard<std::mutex> locker(mMutex);
            if (!mTimers.empty()) {
                auto now = std::chrono::steady_clock::now();
                tsptr = nullptr;
                if (now <= mTimers.front()->mNext) {
                    auto when = mTimers.front()->mNext - now;
                    ts = std::chrono::duration_cast<timespec>(when);
                    if (hasTimeout && (ets.tv_sec < ts.tv_sec || (ets.tv_sec == ts.tv_sec && ets.tv_nsec < ts.tv_nsec))) {
                        tsptr = &ets;
                    }
                } else {
                    memset(&ts, 0, sizeof(ts));
                }
                if (tsptr == nullptr) {
                    tsptr = &ts;
                }
            } else if (hasTimeout) {
                tsptr = &ets;
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

                eintrwrap(e, ::close(fd));
            } else {
                switch (filter) {
                case EVFILT_READ:
                    // read event
                    if (fd == mWakeup[0]) {
                        // read and discard
                        char c;
                        do {
                            eintrwrap(e, read(fd, &c, 1));
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

        if (hasTimeout) {
            const int64_t now = timeNow();
            executeTimeout -= (now - startTime);
            startTime = now;

            if (executeTimeout <= 0) {
                return 0;
            }
        }

        timers.clear();
    }
    return 0;
}
