include(FetchContent)

# Fetch Google Benchmark directly from GitHub
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark testing" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable benchmark warnings as errors for compiler compatibility" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

target_compile_options(benchmark PRIVATE
  $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c2y-extensions>
)
target_compile_options(benchmark_main PRIVATE
  $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c2y-extensions>
)

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
