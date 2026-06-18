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

# Link against base library and benchmark_main (which provides main() automatically)
target_link_libraries(LogStream_bench PRIVATE chaoxi::base benchmark::benchmark_main)
