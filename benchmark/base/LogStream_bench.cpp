#include "chaoxi/base/LogStream.hpp"

#include <cinttypes>
#include <cstdio>
#include <format>
#include <sstream>

#include <benchmark/benchmark.h>

// ============================================================================
// int benchmark
// ============================================================================
static void BM_Printf_Int(benchmark::State& state) {
    char buf[32];
    int i = 0;
    for (auto _ : state) {
        snprintf(buf, sizeof buf, "%d", i++);
        benchmark::DoNotOptimize(buf);
    }
}

BENCHMARK(BM_Printf_Int);

static void BM_Format_Int(benchmark::State& state) {
    char buf[32];
    int i = 0;
    for (auto _ : state) {
        auto result = std::format_to_n(buf, sizeof(buf), "{}", i++);
        buf[result.size] = '\0';  // 手动终止
        benchmark::DoNotOptimize(buf);
    }
}

BENCHMARK(BM_Format_Int);

static void BM_StringStream_Int(benchmark::State& state) {
    std::ostringstream os;
    int i = 0;
    for (auto _ : state) {
        os << i++;
        os.seekp(0, std::ios_base::beg);
        benchmark::DoNotOptimize(os);
    }
}

BENCHMARK(BM_StringStream_Int);

static void BM_LogStream_Int(benchmark::State& state) {
    chaoxi::LogStream os;
    int i = 0;
    for (auto _ : state) {
        os << i++;
        os.resetBuffer();
        benchmark::DoNotOptimize(os);
    }
}

BENCHMARK(BM_LogStream_Int);

// ============================================================================
// double benchmark
// ============================================================================
static void BM_Printf_Double(benchmark::State& state) {
    char buf[32];
    double i = 0.0;
    for (auto _ : state) {
        snprintf(buf, sizeof buf, "%.12g", i += 1.0);
        benchmark::DoNotOptimize(buf);
    }
}

BENCHMARK(BM_Printf_Double);

static void BM_Format_Double(benchmark::State& state) {
    char buf[32];
    double i = 0.0;
    for (auto _ : state) {
        auto result = std::format_to_n(buf, sizeof(buf), "{:.12g}", i += 1.0);
        buf[result.size] = '\0';  // 手动终止
        benchmark::DoNotOptimize(buf);
    }
}

BENCHMARK(BM_Format_Double);

static void BM_StringStream_Double(benchmark::State& state) {
    std::ostringstream os;
    double i = 0.0;
    for (auto _ : state) {
        os << (i += 1.0);
        os.seekp(0, std::ios_base::beg);
        benchmark::DoNotOptimize(os);
    }
}

BENCHMARK(BM_StringStream_Double);

static void BM_LogStream_Double(benchmark::State& state) {
    chaoxi::LogStream os;
    double i = 0.0;
    for (auto _ : state) {
        os << (i += 1.0);
        os.resetBuffer();
        benchmark::DoNotOptimize(os);
    }
}

BENCHMARK(BM_LogStream_Double);

// ============================================================================
// int64_t benchmark
// ============================================================================
static void BM_Printf_Int64(benchmark::State& state) {
    char buf[32];
    int64_t i = 0;
    for (auto _ : state) {
        snprintf(buf, sizeof buf, "%" PRId64, i++);
        benchmark::DoNotOptimize(buf);
    }
}

BENCHMARK(BM_Printf_Int64);

static void BM_Format_Int64(benchmark::State& state) {
    char buf[32];
    int64_t i = 0.0;
    for (auto _ : state) {
        auto result = std::format_to_n(buf, sizeof(buf), "{}", i++);
        buf[result.size] = '\0';  // 手动终止
        benchmark::DoNotOptimize(buf);
    }
}

BENCHMARK(BM_Format_Int64);

static void BM_StringStream_Int64(benchmark::State& state) {
    std::ostringstream os;
    int64_t i = 0;
    for (auto _ : state) {
        os << i++;
        os.seekp(0, std::ios_base::beg);
        benchmark::DoNotOptimize(os);
    }
}

BENCHMARK(BM_StringStream_Int64);

static void BM_LogStream_Int64(benchmark::State& state) {
    chaoxi::LogStream os;
    int64_t i = 0;
    for (auto _ : state) {
        os << i++;
        os.resetBuffer();
        benchmark::DoNotOptimize(os);
    }
}

BENCHMARK(BM_LogStream_Int64);

// ============================================================================
// void* benchmark
// ============================================================================
static void BM_Printf_VoidPtr(benchmark::State& state) {
    char buf[32];
    uintptr_t i = 0;
    for (auto _ : state) {
        snprintf(buf, sizeof buf, "%p", reinterpret_cast<void*>(i++));
        benchmark::DoNotOptimize(buf);
    }
}

BENCHMARK(BM_Printf_VoidPtr);

static void BM_Format_VoidPtr(benchmark::State& state) {
    char buf[32];
    uintptr_t i = 0;
    for (auto _ : state) {
        auto result = std::format_to_n(buf, sizeof(buf), "{}",
                                       reinterpret_cast<void*>(i++));
        buf[result.size] = '\0';  // 手动终止
        benchmark::DoNotOptimize(buf);
    }
}

BENCHMARK(BM_Format_VoidPtr);

static void BM_StringStream_VoidPtr(benchmark::State& state) {
    std::ostringstream os;
    uintptr_t i = 0;
    for (auto _ : state) {
        os << reinterpret_cast<void*>(i++);
        os.seekp(0, std::ios_base::beg);
        benchmark::DoNotOptimize(os);
    }
}

BENCHMARK(BM_StringStream_VoidPtr);

static void BM_LogStream_VoidPtr(benchmark::State& state) {
    chaoxi::LogStream os;
    uintptr_t i = 0;
    for (auto _ : state) {
        os << reinterpret_cast<void*>(i++);
        os.resetBuffer();
        benchmark::DoNotOptimize(os);
    }
}

BENCHMARK(BM_LogStream_VoidPtr);
