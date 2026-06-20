include(FetchContent)

# Fetch Google Benchmark directly from GitHub
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark testing" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(LogStream_bench base/LogStream_bench.cpp)
target_link_libraries(LogStream_bench PRIVATE chaoxi::base benchmark::benchmark_main)

add_executable(Logging_bench base/Logging_bench.cpp)
target_link_libraries(Logging_bench PRIVATE chaoxi::base benchmark::benchmark_main)

add_executable(LogFile_bench base/LogFile_bench.cpp)
target_link_libraries(LogFile_bench PRIVATE chaoxi::base benchmark::benchmark_main)

add_executable(AsyncLogging_bench base/AsyncLogging_bench.cpp)
target_link_libraries(AsyncLogging_bench PRIVATE chaoxi::base benchmark::benchmark_main)

add_executable(BlockingQueue_bench base/BlockingQueue_bench.cpp)
target_link_libraries(BlockingQueue_bench PRIVATE chaoxi::base benchmark::benchmark_main)

add_executable(BoundedBlockingQueue_bench base/BoundedBlockingQueue_bench.cpp)
target_link_libraries(BoundedBlockingQueue_bench PRIVATE chaoxi::base benchmark::benchmark_main)

# ----------------------------------------------------------------------------
# Benchmark                                  Time             CPU   Iterations
# ----------------------------------------------------------------------------
# PingPongFixture/Latency_PingPong        4286 ns         1937 ns       350734
# BM_Queue_Throughput/1/1/real_time    1666549 ns        21691 ns          394 items_per_second=6.00042M/s
# BM_Queue_Throughput/4/1/real_time    6861969 ns        38407 ns           74 items_per_second=5.82923M/s
# BM_Queue_Throughput/1/4/real_time    5943188 ns        31423 ns           98 items_per_second=1.6826M/s
# BM_Queue_Throughput/4/4/real_time   10599101 ns        39186 ns           65 items_per_second=3.7739M/s
# BM_Queue_Throughput/8/8/real_time   23719842 ns       114129 ns           27 items_per_second=3.3727M/s
# BM_Queue_HotPotato/2/real_time     193155301 ns        13929 ns            3 items_per_second=517.718k/s
# BM_Queue_HotPotato/4/real_time     395593980 ns        19580 ns            3 items_per_second=252.784k/s
# BM_Queue_HotPotato/8/real_time     269234858 ns        25110 ns            2 items_per_second=371.423k/s
# BM_Queue_HotPotato/16/real_time    403721620 ns        17733 ns            2 items_per_second=247.695k/s