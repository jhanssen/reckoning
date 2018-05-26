cmake_minimum_required(VERSION 3.8)
include_directories(${CMAKE_CURRENT_LIST_DIR})

include_sources(SOURCES event)

add_library(reckoning SHARED ${SOURCES})
