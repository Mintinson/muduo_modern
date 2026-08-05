add_executable(V2Task_test Task_test.cpp)
target_link_libraries(V2Task_test PRIVATE chaoxi::v2 GTest::gtest_main)
chaoxi_target_enable_warnings(V2Task_test)

add_executable(V2Sleep_test Sleep_test.cpp)
target_link_libraries(V2Sleep_test PRIVATE chaoxi::v2 GTest::gtest_main)
chaoxi_target_enable_warnings(V2Sleep_test)

add_executable(V2AsyncFd_test AsyncFd_test.cpp)
target_link_libraries(V2AsyncFd_test PRIVATE chaoxi::v2 GTest::gtest_main)
chaoxi_target_enable_warnings(V2AsyncFd_test)

add_executable(V2AsyncSocket_test AsyncSocket_test.cpp)
target_link_libraries(V2AsyncSocket_test PRIVATE chaoxi::v2 GTest::gtest_main)
chaoxi_target_enable_warnings(V2AsyncSocket_test)

add_executable(V2TcpServer_test TcpServer_test.cpp)
target_link_libraries(V2TcpServer_test PRIVATE chaoxi::v2 GTest::gtest_main)
chaoxi_target_enable_warnings(V2TcpServer_test)

include(GoogleTest)
gtest_discover_tests(V2Task_test TEST_PREFIX "v2." PROPERTIES TIMEOUT 5)
gtest_discover_tests(V2Sleep_test TEST_PREFIX "v2." PROPERTIES TIMEOUT 5)
gtest_discover_tests(V2AsyncFd_test TEST_PREFIX "v2." PROPERTIES TIMEOUT 5)
gtest_discover_tests(V2AsyncSocket_test TEST_PREFIX "v2." PROPERTIES TIMEOUT 5)
gtest_discover_tests(V2TcpServer_test TEST_PREFIX "v2." PROPERTIES TIMEOUT 5)
