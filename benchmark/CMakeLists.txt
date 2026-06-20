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
