#include <event/Loop.h>
#include <event/Timeval.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <log/Log.h>
#include <util/Socket.h>
#include <net/Resolver.h>

#ifdef __linux__
#include <linux/version.h>
#if !defined EPOLLRDHUP && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17)
// EPOLLRDHUP exists in the kernel since 2.6.17. Just define it here:
// (see: https://sourceware.org/bugzilla/show_bug.cgi?id=5040)
#define EPOLLRDHUP 0x2000
#endif
#endif // __linux__

using namespace reckoning;
using namespace reckoning::event;
using namespace reckoning::log;

void Loop::init()
{
    tLoop = shared_from_this();

    mFd = epoll_create1(0);
    if (mFd == -1) {
        Log(Log::Error) << "unable to open eventloop epoll_create1" << errno;
        cleanup();
        return;
    }

    commonInit();

    int e;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = mWakeup[0];
    e = epoll_ctl(mFd, EPOLL_CTL_ADD, mWakeup[0], &ev);

    if (e == -1) {
        Log(Log::Error) << "unable to add wakeup pipe to epoll" << errno;
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

    int epollTimeout;

    std::vector<std::unique_ptr<Event> > events;
    std::vector<std::shared_ptr<Timer> > timers;
    std::vector<int> fds;

    enum { MaxEvents = 64 };
    struct epoll_event epevents[MaxEvents];
    int e;
    for (;;) {
        // execute all events
        for (;;) {
            {
                std::lock_guard<std::mutex> locker(mMutex);
                // are we stopped?
                if (mStopped) {
                    // shutdown threads etc
                    return mStatus;
                }

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
            std::lock_guard<std::mutex> locker(mMutex);
            // did one of the events stop us?
            if (mStopped) {
                // shutdown threads etc
                return mStatus;
            }

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
            struct epoll_event ev;
            memset(&ev, 0, sizeof(struct epoll_event));
            for (auto fd : fds) {
                ev.events = EPOLLRDHUP | EPOLLET | EPOLLIN;
                ev.data.fd = fd;
                e = epoll_ctl(mFd, EPOLL_CTL_ADD, fd, &ev);
            }
            fds.clear();
        }
        // when is our first timer?
        {
            std::lock_guard<std::mutex> locker(mMutex);
            if (!mTimers.empty()) {
                auto now = std::chrono::steady_clock::now();
                if (now <= mTimers.front()->mNext) {
                    const auto when = mTimers.front()->mNext - now;
                    const auto mscount = std::chrono::duration_cast<std::chrono::milliseconds>(when).count();
                    epollTimeout = std::min<int>(mscount, std::numeric_limits<int>::max());
                } else {
                    epollTimeout = 0;
                }
            } else {
                epollTimeout = hasTimeout ? executeTimeout : -1;
            }

            // also, while we have the mutex, process removed fds
            fds = std::move(mRemovedFds);

            // and update sockets, hopefully we won't have too many of these
            if (!mUpdateFds.empty()) {
                struct epoll_event ev;
                memset(&ev, 0, sizeof(struct epoll_event));
                for (auto update : mUpdateFds) {
                    ev.data.fd = update.first;
                    ev.events = EPOLLRDHUP | EPOLLET;
                    if (update.second & FdRead)
                        ev.events |= EPOLLIN;
                    if (update.second & FdWrite)
                        ev.events |= EPOLLOUT;
                    e = epoll_ctl(mFd, EPOLL_CTL_MOD, update.first, &ev);
                }
                mUpdateFds.clear();
            }
        }
        // remove fds
        if (!fds.empty()) {
            struct epoll_event ev;
            memset(&ev, 0, sizeof(struct epoll_event));
            for (auto fd : fds) {
                e = epoll_ctl(mFd, EPOLL_CTL_DEL, fd, &ev);
                eintrwrap(e, close(fd));
            }
            fds.clear();
        }

        eintrwrap(e, epoll_wait(mFd, epevents, MaxEvents, std::min(executeTimeout, epollTimeout)));
        if (e < 0) {
            // bad
            Log(Log::Error) << "unable to wait for events" << errno;
            cleanup();
            return -1;
        }

        // printf("got %d events\n", e);

        for (int i = 0; i < e; ++i) {
            const uint32_t ev = epevents[i].events;
            const int fd = epevents[i].data.fd;
            if (ev & (EPOLLERR | EPOLLHUP) && !(ev & EPOLLRDHUP)) {
                // badness, we want this thing out
                epoll_ctl(mFd, EPOLL_CTL_DEL, fd, &epevents[i]);
                processFd(fd, FdError);
                eintrwrap(e, ::close(fd));
                continue;
            }
            if (ev & (EPOLLIN | EPOLLRDHUP)) {
                // read event
                if (fd == mWakeup[0]) {
                    // read and discard
                    char c;
                    do {
                        e = read(fd, &c, 1);
                        if (e == 1 && c == 'q') {
                            // we want to quit
                            std::lock_guard<std::mutex> locker(mMutex);
                            mStopped = true;
                        }
                    } while (e == 1);
                } else {
                    processFd(fd, FdRead);
                }
            }
            if (ev & EPOLLOUT) {
                // write event
                processFd(fd, FdWrite);
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

        if (hasTimeout) {
            const int64_t now = timeNow();
            executeTimeout -= (now - startTime);
            startTime = now;

            if (executeTimeout <= 0) {
                return 0;
            }
        }
    }
    return 0;
}
