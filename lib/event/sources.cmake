set(EVENT_SOURCES Loop.cpp)

if (HAVE_KQUEUE)
    list(APPEND EVENT_SOURCES Loop_kqueue.cpp)
elseif (HAVE_EPOLL)
    list(APPEND EVENT_SOURCES Loop_epoll.cpp)
endif()
