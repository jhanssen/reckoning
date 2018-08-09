cmake_minimum_required(VERSION 3.9)
include_directories(${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

if (APPLE)
    set(OPENSSL_ROOT_DIR /usr/local/opt/openssl)
endif()
include(FindOpenSSL)

include_sources(SOURCES event)
include_sources(SOURCES fs)
include_sources(SOURCES log)
include_sources(SOURCES net)
include_sources(SOURCES serializer)
include_sources(SOURCES util)

add_library(reckoning SHARED ${SOURCES})
target_link_libraries(reckoning c-ares::cares_shared OpenSSL::Crypto wslay_shared pthread)
