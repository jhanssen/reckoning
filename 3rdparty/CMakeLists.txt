cmake_minimum_required(VERSION 3.9)

set(WSLAY_SHARED ON)
set(WSLAY_STATIC OFF)

set(CMAKE_POLICY_DEFAULT_CMP0042 OLD)

add_subdirectory(cares)
add_subdirectory(wslay)

target_compile_options(wslay_shared PRIVATE -include;arpa/inet.h)
