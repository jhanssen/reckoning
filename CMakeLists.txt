cmake_minimum_required(VERSION 3.9)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(RECKONING_EXAMPLES "Build examples" ON)

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

check_c_source_compiles("
    #include <sys/epoll.h>
    int main(int argc, char** argv) {
        int fd = epoll_create1(0);
        return 0;
    }"
    HAVE_EPOLL)

check_c_source_compiles("
    #include <fcntl.h>
    int main(int argc, char** argv) {
        int e = fcntl(0, F_SETFL, O_NONBLOCK);
        return e;
    }"
    HAVE_NONBLOCK)

check_c_source_compiles("
    #include <sys/socket.h>
    int main(int argc, char** argv) {
        int flags = 1;
        setsockopt(0, SOL_SOCKET, SO_NOSIGPIPE, (void *)&flags, sizeof(int));
        return flags;
    }"
    HAVE_NOSIGPIPE)

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

check_cxx_source_compiles("
    #include <any>
    int main(int argc, char** argv)
    {
        std::any a = 10;
        return std::any_cast<int>(a);
    }"
    HAVE_ANY_CAST)

check_cxx_source_compiles("
    #include <experimental/any>
    int main(int argc, char** argv)
    {
        auto a = std::experimental::any(10);
        return *std::experimental::any_cast<int>(&a);
    }"
    HAVE_EXPERIMENTAL_ANY_CAST)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/cmake/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/config.h
    )

add_subdirectory(3rdparty)
add_subdirectory(lib)
if (RECKONING_EXAMPLES)
    add_subdirectory(examples)
endif()
