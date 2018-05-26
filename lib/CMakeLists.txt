cmake_minimum_required(VERSION 3.9)
include_directories(${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

include_sources(SOURCES event)
include_sources(SOURCES log)

add_library(reckoning SHARED ${SOURCES})
