include(FetchContent)

# Fetch GoogleTest directly from GitHub
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
)
# For Windows/MSVC compatibility (usually good practice to leave in)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

if(CHAOXI_BUILD_V2)
    add_subdirectory(v2 v2)
endif()

add_executable(LogStream_test base/LogStream_test.cpp)
target_link_libraries(LogStream_test PRIVATE chaoxi::base GTest::gtest_main)

add_executable(LogFile_test base/LogFile_test.cpp)
target_link_libraries(LogFile_test PRIVATE chaoxi::base GTest::gtest_main)

add_executable(AsyncLogging_test base/AsyncLogging_test.cpp)
target_link_libraries(AsyncLogging_test PRIVATE chaoxi::base GTest::gtest_main)

add_executable(BlockingQueue_test base/BlockingQueue_test.cpp)
target_link_libraries(BlockingQueue_test PRIVATE chaoxi::base GTest::gtest_main)

# add_executable(BoundedBlockingQueue_test base/BoundedBlockingQueue_test.cpp)
# target_link_libraries(BoundedBlockingQueue_test PRIVATE chaoxi::base GTest::gtest_main)

add_executable(BaseUtilities_test base/BaseUtilities_test.cpp)
target_link_libraries(BaseUtilities_test PRIVATE chaoxi::base GTest::gtest_main)

add_executable(InetAddress_test net/InetAddress_test.cpp)
target_link_libraries(InetAddress_test PRIVATE chaoxi::net GTest::gtest_main)

if(WIN32)
    add_executable(WindowsPlatform_test net/WindowsPlatform_test.cpp)
    target_link_libraries(WindowsPlatform_test PRIVATE chaoxi::net GTest::gtest_main)
else()
    add_executable(SocketOps_test net/SocketOps_test.cpp)
    target_link_libraries(SocketOps_test PRIVATE chaoxi::net GTest::gtest_main)
endif()

add_executable(Buffer_test net/Buffer_test.cpp)
target_link_libraries(Buffer_test PRIVATE chaoxi::net GTest::gtest_main)

if(NOT WIN32)
    add_executable(EventLoop_test net/EventLoop_test.cpp)
    target_link_libraries(EventLoop_test PRIVATE chaoxi::net)
endif()

add_executable(EventLoopThread_test net/EventLoopThread_test.cpp)
target_link_libraries(EventLoopThread_test PRIVATE chaoxi::net)

if(NOT WIN32)
    add_executable(TcpConnection_test net/TcpConnection_test.cpp)
    target_link_libraries(TcpConnection_test PRIVATE chaoxi::net GTest::gtest_main)
endif()

add_executable(EventLoopThreadPool_test net/EventLoopThreadPool_test.cpp)
target_link_libraries(EventLoopThreadPool_test PRIVATE chaoxi::net GTest::gtest_main)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(EPollPoller_test net/EPollPoller_test.cpp)
    target_link_libraries(EPollPoller_test PRIVATE chaoxi::net GTest::gtest_main)
endif()

# add_executable(Async_demo base/Async_demo.cpp)
# target_link_libraries(Async_demo PRIVATE chaoxi::base)

add_executable(HTTP_test net/http/Http_test.cpp)
target_link_libraries(HTTP_test PRIVATE chaoxi::net GTest::gtest_main)

# Use CMake's built-in GoogleTest module to discover tests automatically
include(GoogleTest)
gtest_discover_tests(LogStream_test)
# gtest_discover_tests(LogFile_test)
gtest_discover_tests(AsyncLogging_test)
if(WIN32)
    gtest_discover_tests(WindowsPlatform_test)
else()
    gtest_discover_tests(SocketOps_test)
endif()
gtest_discover_tests(InetAddress_test)
gtest_discover_tests(Buffer_test)
if(NOT WIN32)
    gtest_discover_tests(TcpConnection_test)
endif()
gtest_discover_tests(EventLoopThreadPool_test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    gtest_discover_tests(EPollPoller_test)
endif()
gtest_discover_tests(HTTP_test)
