cmake_minimum_required(VERSION 3.30)

project(chaoxi LANGUAGES CXX)

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# === 项目配置选项 ===
option(CHAOXI_BUILD_EXAMPLES "Build chaoxi examples" ON)
option(CHAOXI_BUILD_TESTS "Build chaoxi tests" ON)
option(CHAOXI_BUILD_BENCHMARKS "Build chaoxi benchmarks" ON)
option(CHAOXI_BUILD_V2 "Build the coroutine-based chaoxi v2 API" ON)
option(CHAOXI_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings for chaoxi targets" ON)
option(CHAOXI_WARNINGS_AS_ERRORS "Treat warnings as errors for chaoxi targets" ON)

message(STATUS "chaoxi coroutine v2: ${CHAOXI_BUILD_V2}")

# if(PROJECT_IS_TOP_LEVEL)
#     message("hhhh===========")
#     find_program(CLANG_TIDY_EXE clang-tidy REQUIRED)
#     find_program(RUN_CLANG_TIDY_EXE run-clang-tidy REQUIRED)

#     add_custom_target(run-clang-tidy
#         COMMAND ${RUN_CLANG_TIDY_EXE}
#             -clang-tidy-binary ${CLANG_TIDY_EXE}
#             -p ${CMAKE_BINARY_DIR}
#     )
# endif()

# === 1. 核心库 ===
add_subdirectory(chaoxi)

# === 2. 示例 (Examples) ===
if(CHAOXI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# === 3. 测试 (Tests) ===
if(CHAOXI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# === 4. 基准测试 (Benchmarks) ===
if(CHAOXI_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Benchmarking in Debug mode may produce inaccurate results. Consider using Release mode for benchmarking.")
    endif()
endif()

