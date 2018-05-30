cmake_minimum_required(VERSION 3.9)

set(WSLAY_SHARED ON CACHE BOOL "Build shared version of the library" FORCE)
set(WSLAY_STATIC OFF CACHE BOOL "Build static version of the library" FORCE)
set(WSLAY_CONFIGURE_INSTALL OFF CACHE BOOL "Generate install target" FORCE)

set(CMAKE_POLICY_DEFAULT_CMP0042 NEW)

add_subdirectory(cares)
add_subdirectory(wslay)

target_compile_options(wslay_shared PRIVATE -include;arpa/inet.h)
