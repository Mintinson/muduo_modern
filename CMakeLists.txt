cmake_minimum_required(VERSION 3.30)

project(chaoxi VERSION 0.2.0 LANGUAGES CXX)

# ============================================================
# Project defaults
# ============================================================

if(PROJECT_IS_TOP_LEVEL)
    set(CHAOXI_DEVELOPER_DEFAULT ON)
else()
    set(CHAOXI_DEVELOPER_DEFAULT OFF)
endif()

option(CHAOXI_BUILD_EXAMPLES "Build chaoxi examples" ${CHAOXI_DEVELOPER_DEFAULT})
option(CHAOXI_BUILD_TESTS "Build chaoxi tests" ${CHAOXI_DEVELOPER_DEFAULT})
option(CHAOXI_BUILD_BENCHMARKS "Build chaoxi benchmarks" ${CHAOXI_DEVELOPER_DEFAULT})
option(CHAOXI_BUILD_V2 "Build the coroutine-based chaoxi v2 API" OFF)
option(CHAOXI_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings for chaoxi targets" ${CHAOXI_DEVELOPER_DEFAULT})
option(CHAOXI_WARNINGS_AS_ERRORS "Treat warnings as errors for chaoxi targets" ${CHAOXI_DEVELOPER_DEFAULT})

# 作为独立项目时默认提供 install；
# 作为 FetchContent/add_subdirectory 子项目时默认不污染父项目安装规则。
option(CHAOXI_INSTALL "Generate install rules for chaoxi" ${PROJECT_IS_TOP_LEVEL})


# ============================================================
# Compiler settings
# ============================================================

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# ============================================================
# Library
# ============================================================
add_subdirectory(chaoxi)


# ============================================================
# Installation / package config
# ============================================================

if(CHAOXI_INSTALL)
    include(cmake/ChaoxiInstall.cmake)
endif()


message(STATUS "chaoxi coroutine v2: ${CHAOXI_BUILD_V2}")


# ============================================================
# Examples
# ============================================================

if(CHAOXI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()


# ============================================================
# Tests
# ============================================================

if(CHAOXI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================
# Benchmarks
# ============================================================

if(CHAOXI_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Benchmarking in Debug mode may produce inaccurate results."
        "Consider using Release mode for benchmarking.")
    endif()
endif()

