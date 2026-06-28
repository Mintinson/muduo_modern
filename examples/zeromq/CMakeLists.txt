add_executable(zeromq_local_lat local_lat.cpp)
target_link_libraries(zeromq_local_lat PRIVATE chaoxi::chaoxi)

# add_executable(zeromq_local_bench local_bench.cpp)
# target_link_libraries(zeromq_local_bench PRIVATE chaoxi::chaoxi)
# target_link_libraries(pingpong_bench PRIVATE chaoxi::chaoxi)
