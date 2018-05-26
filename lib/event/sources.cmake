set(EVENT_SOURCES EventLoop.cpp)

if (HAVE_KQUEUE)
    list(APPEND EVENT_SOURCES EventLoop_kqueue.cpp)
endif()
