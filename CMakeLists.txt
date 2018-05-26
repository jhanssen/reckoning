cmake_minimum_required(VERSION 3.9)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)
include(cmake/functions.cmake)

include_directories(${CMAKE_CURRENT_BINARY_DIR}/cmake)

check_c_source_compiles("
    #include <sys/types.h>
    #include <sys/event.h>
    #include <sys/time.h>
    int main(int argc, char** argv) {
        int fd = kqueue();
        return 0;
    }"
    HAVE_KQUEUE)

check_cxx_source_compiles("
    #include <sys/types.h>
    #include <sys/event.h>
    #include <sys/time.h>
    int main(int argc, char** argv) {
        int ret = kevent64(0, nullptr, 0, nullptr, 0, 0, nullptr);
        return 0;
    }"
    HAVE_KEVENT64)

check_cxx_source_compiles("
    #include <type_traits>
    template<typename T, typename std::enable_if<std::is_invocable_r<void, T>::value, void>::type* = nullptr>
    void post(T&& func) { }
    int main(int argc, char** argv) {
    }"
    HAVE_INVOCABLE_R)

check_cxx_source_compiles("
    #include <type_traits>
    template<typename T, typename std::enable_if<std::__invokable_r<void, T>::value, void>::type* = nullptr>
    void post(T&& func) { }
    int main(int argc, char** argv) {
    }"
    HAVE_INVOKABLE_R)

add_subdirectory(lib)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/cmake/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/config.h
    )
