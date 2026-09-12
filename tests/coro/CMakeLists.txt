add_executable(CoroTask_test Task_test.cpp)
target_link_libraries(CoroTask_test PRIVATE chaoxi::coro GTest::gtest_main)
chaoxi_target_enable_warnings(CoroTask_test)

add_executable(CoroSleep_test Sleep_test.cpp)
target_link_libraries(CoroSleep_test PRIVATE chaoxi::coro GTest::gtest_main)
chaoxi_target_enable_warnings(CoroSleep_test)

add_executable(CoroAsyncFd_test AsyncFd_test.cpp)
target_link_libraries(CoroAsyncFd_test PRIVATE chaoxi::coro GTest::gtest_main)
chaoxi_target_enable_warnings(CoroAsyncFd_test)

add_executable(CoroAsyncSocket_test AsyncSocket_test.cpp)
target_link_libraries(CoroAsyncSocket_test PRIVATE chaoxi::coro GTest::gtest_main)
chaoxi_target_enable_warnings(CoroAsyncSocket_test)

add_executable(CoroTcpServer_test TcpServer_test.cpp)
target_link_libraries(CoroTcpServer_test PRIVATE chaoxi::coro GTest::gtest_main)
chaoxi_target_enable_warnings(CoroTcpServer_test)

include(GoogleTest)
gtest_discover_tests(CoroTask_test TEST_PREFIX "coro." PROPERTIES TIMEOUT 5)
gtest_discover_tests(CoroSleep_test TEST_PREFIX "coro." PROPERTIES TIMEOUT 5)
gtest_discover_tests(CoroAsyncFd_test TEST_PREFIX "coro." PROPERTIES TIMEOUT 5)
gtest_discover_tests(CoroAsyncSocket_test TEST_PREFIX "coro." PROPERTIES TIMEOUT 5)
gtest_discover_tests(CoroTcpServer_test TEST_PREFIX "coro." PROPERTIES TIMEOUT 5)
