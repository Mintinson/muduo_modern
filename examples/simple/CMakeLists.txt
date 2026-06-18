add_executable(simple_echo echo/main.cpp echo/echo.cpp)
# 使用现代 CMake 的命名空间方式链接，清晰指明来自 chaoxi 核心库
target_link_libraries(simple_echo PRIVATE chaoxi::chaoxi)

add_executable(demo demo.cpp)

add_executable(s00_test1 s00/s00_test1.cpp)
target_link_libraries(s00_test1 PRIVATE chaoxi::chaoxi)

add_executable(s04_acceptor_1 s04/acceptor_1.cpp)
target_link_libraries(s04_acceptor_1 PRIVATE chaoxi::chaoxi)

add_executable(tcp_connection_demo s01_tcp_connection/tcp_connection_demo.cpp)
target_link_libraries(tcp_connection_demo PRIVATE chaoxi::chaoxi)

add_executable(echo_server_raw s01_tcp_connection/echo_server_raw.cpp)
target_link_libraries(echo_server_raw PRIVATE chaoxi::chaoxi)

add_executable(s04_connector s04/connector_1.cpp)
target_link_libraries(s04_connector PRIVATE chaoxi::chaoxi)

add_executable(epoll_demo s05_epoll/epoll_demo.cpp)
target_link_libraries(epoll_demo PRIVATE chaoxi::chaoxi)