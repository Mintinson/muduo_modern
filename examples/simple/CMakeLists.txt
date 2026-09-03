add_executable(simple_echo echo/main.cpp echo/echo.cpp)
# 使用现代 CMake 的命名空间方式链接，清晰指明来自 chaoxi 核心库
target_link_libraries(simple_echo PRIVATE chaoxi::chaoxi)

add_executable(simple_discard discard/discard.cpp discard/main.cpp)
target_link_libraries(simple_discard PRIVATE chaoxi::chaoxi)

add_executable(simple_daytime daytime/daytime.cpp daytime/main.cpp)
target_link_libraries(simple_daytime PRIVATE chaoxi::chaoxi)

add_executable(simple_time time/time.cpp time/main.cpp)
target_link_libraries(simple_time PRIVATE chaoxi::chaoxi)

add_executable(simple_time_client time/time_client.cpp)
target_link_libraries(simple_time_client PRIVATE chaoxi::chaoxi)

add_executable(simple_chargen chargen/chargen.cpp chargen/main.cpp)
target_link_libraries(simple_chargen PRIVATE chaoxi::chaoxi)

add_executable(simple_all_in_one
    all_in_one/all_in_one.cpp
    echo/echo.cpp
    discard/discard.cpp
    daytime/daytime.cpp
    time/time.cpp
    chargen/chargen.cpp)
target_link_libraries(simple_all_in_one PRIVATE chaoxi::chaoxi)



if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(s00_test1 s00/s00_test1.cpp)
    target_link_libraries(s00_test1 PRIVATE chaoxi::chaoxi)
endif()


# add_executable(s04_acceptor_1 s04/acceptor_1.cpp)
# target_link_libraries(s04_acceptor_1 PRIVATE chaoxi::chaoxi)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(tcp_connection_demo s01_tcp_connection/tcp_connection_demo.cpp)
    target_link_libraries(tcp_connection_demo PRIVATE chaoxi::chaoxi)

    add_executable(echo_server_raw s01_tcp_connection/echo_server_raw.cpp)
    target_link_libraries(echo_server_raw PRIVATE chaoxi::chaoxi)
endif()

add_executable(s04_connector s04/connector_1.cpp)
target_link_libraries(s04_connector PRIVATE chaoxi::chaoxi)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(epoll_demo s05_epoll/epoll_demo.cpp)
    target_link_libraries(epoll_demo PRIVATE chaoxi::chaoxi)
endif()
