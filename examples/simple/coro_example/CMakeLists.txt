# examples/simple 中“可以用协程改造”的例子对应的协程版实现。
# 每个可执行文件都与同级目录下的回调版功能等价，只是改用 chaoxi::coro 的
# Task / AsyncSocket / TcpServer 表达异步流程。
#
# 本目录只会在 CHAOXI_BUILD_CORO=ON 时被 examples/simple/CMakeLists.txt 引入。

add_executable(coro_simple_echo echo.cpp)
target_link_libraries(coro_simple_echo PRIVATE chaoxi::coro)

add_executable(coro_simple_discard discard.cpp)
target_link_libraries(coro_simple_discard PRIVATE chaoxi::coro)

add_executable(coro_simple_daytime daytime.cpp)
target_link_libraries(coro_simple_daytime PRIVATE chaoxi::coro)

add_executable(coro_simple_time_server time.cpp)
target_link_libraries(coro_simple_time_server PRIVATE chaoxi::coro)

add_executable(coro_simple_time_client time_client.cpp)
target_link_libraries(coro_simple_time_client PRIVATE chaoxi::coro)

add_executable(coro_simple_chargen chargen.cpp)
target_link_libraries(coro_simple_chargen PRIVATE chaoxi::coro)

add_executable(coro_simple_acceptor acceptor.cpp)
target_link_libraries(coro_simple_acceptor PRIVATE chaoxi::coro)

add_executable(coro_simple_connector connector.cpp)
target_link_libraries(coro_simple_connector PRIVATE chaoxi::coro)
