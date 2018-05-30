set(EVENT_SOURCES EventLoop.cpp)

if (HAVE_KQUEUE)
    list(APPEND EVENT_SOURCES EventLoop_kqueue.cpp)
elseif (HAVE_EPOLL)
    list(APPEND EVENT_SOURCES EventLoop_epoll.cpp)
endif()
