cmake_minimum_required(VERSION 3.9)

set(WSLAY_SHARED ON CACHE BOOL "Build shared version of the library" FORCE)
set(WSLAY_STATIC OFF CACHE BOOL "Build static version of the library" FORCE)
set(WSLAY_CONFIGURE_INSTALL OFF CACHE BOOL "Generate install target" FORCE)
set(URIPARSER_BUILD_DOCS OFF)
set(URIPARSER_BUILD_TESTS OFF)
set(URIPARSER_BUILD_TOOLS OFF)
set(URIPARSER_ENABLE_INSTALL OFF)

set(CMAKE_POLICY_DEFAULT_CMP0042 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

add_subdirectory(cares)
add_subdirectory(wslay)
add_subdirectory(uriparser)

target_compile_options(wslay_shared PRIVATE -include;arpa/inet.h)
