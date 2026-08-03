add_executable(pingpong_client client.cpp)
target_link_libraries(pingpong_client PRIVATE chaoxi::chaoxi)

add_executable(pingpong_server server.cpp)
target_link_libraries(pingpong_server PRIVATE chaoxi::chaoxi)

if(NOT WIN32)
    add_executable(pingpong_bench bench.cpp)
    target_link_libraries(pingpong_bench PRIVATE chaoxi::chaoxi)
endif()
