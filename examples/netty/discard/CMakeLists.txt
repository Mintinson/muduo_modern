add_executable(netty_discard_client client.cpp)
target_link_libraries(netty_discard_client chaoxi::chaoxi)

add_executable(netty_discard_server server.cpp)
target_link_libraries(netty_discard_server chaoxi::chaoxi)

