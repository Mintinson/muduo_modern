///
/// @file benchmark/base/Logging_bench.cpp
/// @brief Logger benchmark —— 对比 LOG_XX vs FLOG_XX + 不同数据类型 +
/// 日志级别过滤
///
/// LOG_XX 和 FLOG_XX 的核心区别：
///   LOG_INFO << val   → 通过 operator<< + to_chars 直接写入 FixedBuffer（栈上
///   4KB） FLOG_INFO("{}",v) → 先调用 std::format 生成临时 string，再 append 到
///   FixedBuffer
///                       std::format 需要额外的动态分配（对长字符串）
///
/// 测试维度：
///   1. 基础类型: int, double, string_view, void*
///   2. 混合类型: int + string
///   3. 日志级别过滤 (DEBUG level when g_logLevel=INFO → should be cheap)
///   4. 完整 Logger 生命周期 (source_location + timestamp + 输出)
///

#include "chaoxi/base/Logging.hpp"

#include <cstdio>
#include <format>

#include <benchmark/benchmark.h>

using namespace chaoxi;

// ============================================================================
// 辅助：设置空输出函数，避免 benchmark 被 I/O 吞噬
// ============================================================================
void nullOutput(std::string_view /*unused*/) {}

void nullFlush() {}

struct LoggingFixture : benchmark::Fixture
{
    void SetUp(::benchmark::State&) override
    {
        Logger::setOutput(nullOutput);
        Logger::setFlush(nullFlush);
    }

    void TearDown(::benchmark::State&) override {}
};

// ============================================================================
// 1. int —— LOG_INFO << int  vs  FLOG_INFO("{}", int)
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_INT)(benchmark::State& state)
{
    int i = 0;
    for (auto _ : state)
    {
        LOG_INFO << i++;
        // Logger 在分号结束时析构，调用 ~Logger → finish → output
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_INT)(benchmark::State& state)
// {
//     int i = 0;
//     for (auto _ : state)
//     {
//         FLOG_INFO("{}", i++);
//     }
// }

// ============================================================================
// 2. double —— LOG_INFO << double  vs  FLOG_INFO("{}", double)
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_DOUBLE)(benchmark::State& state)
{
    double d = 0.0;
    for (auto _ : state)
    {
        LOG_INFO << (d += 1.0);
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_DOUBLE)(benchmark::State& state)
// {
//     double d = 0.0;
//     for (auto _ : state)
//     {
//         FLOG_INFO("{:.12g}", d += 1.0);
//     }
// }

// ============================================================================
// 3. string_view —— LOG_INFO << sv  vs  FLOG_INFO("{}", sv)
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_STRING)(benchmark::State& state)
{
    std::string_view msg = "hello world benchmark test message";
    for (auto _ : state)
    {
        LOG_INFO << msg;
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_STRING)(benchmark::State& state)
// {
//     std::string_view msg = "hello world benchmark test message";
//     for (auto _ : state)
//     {
//         FLOG_INFO("{}", msg);
//     }
// }

// ============================================================================
// 4. void* —— LOG_INFO << ptr  vs  FLOG_INFO("{}", ptr)
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_PTR)(benchmark::State& state)
{
    uintptr_t i = 0;
    for (auto _ : state)
    {
        LOG_INFO << reinterpret_cast<void*>(i++);
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_PTR)(benchmark::State& state)
// {
//     uintptr_t i = 0;
//     for (auto _ : state)
//     {
//         FLOG_INFO("{}", reinterpret_cast<void*>(i++));
//     }
// }

// ============================================================================
// 5. 混合类型: int + string
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_MIXED)(benchmark::State& state)
{
    int i = 0;
    std::string_view msg = "message";
    for (auto _ : state)
    {
        LOG_INFO << i++ << ' ' << msg;
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_MIXED)(benchmark::State& state)
// {
//     int i = 0;
//     std::string_view msg = "message";
//     for (auto _ : state)
//     {
//         FLOG_INFO("{} {}", i++, msg);
//     }
// }

// ============================================================================
// 6. 日志级别过滤 —— DEBUG 在 INFO 级别下被跳过（只评估 if 条件）
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_DEBUG_FILTERED)(benchmark::State& state)
{
    // g_logLevel = INFO (默认), LOG_DEBUG 的 if 条件为 false
    // 只测试 if 判断的开销
    int i = 0;
    for (auto _ : state)
    {
        LOG_DEBUG << i++;  // if (INFO <= DEBUG) → false → 跳过整个 Logger 构造
        benchmark::DoNotOptimize(i);
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_DEBUG_FILTERED)(benchmark::State& state)
// {
//     int i = 0;
//     for (auto _ : state)
//     {
//         FLOG_DEBUG("{}", i++);  // 同 LOG_DEBUG，if 条件过滤
//         benchmark::DoNotOptimize(i);
//     }
// }

// ============================================================================
// 7. Full Logger —— 模拟完整日志输出（含 source_location，不含 I/O）
//    （I/O 已被 nullOutput 替代，但 formatTime + finish 仍在执行）
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_SHORT)(benchmark::State& state)
{
    for (auto _ : state)
    {
        LOG_INFO << "short";
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_SHORT)(benchmark::State& state)
// {
//     for (auto _ : state)
//     {
//         FLOG_INFO("{}", "short");
//     }
// }

// ============================================================================
// 8. 长消息 —— 测试消息长度对性能的影响
// ============================================================================

static constexpr std::string_view kLongMsg =
    "this is a somewhat longer log message to test the performance impact of "
    "message length on the logging system and its internal buffer management";

BENCHMARK_F(LoggingFixture, LOG_LONG)(benchmark::State& state)
{
    for (auto _ : state)
    {
        LOG_INFO << kLongMsg;
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_LONG)(benchmark::State& state)
// {
//     for (auto _ : state)
//     {
//         FLOG_INFO("{}", kLongMsg);
//     }
// }

// ============================================================================
// 9. 省略日志 level —— LOG_WARN (无 if 过滤) vs LOG_INFO (有 if 过滤)
//    LOG_WARN/ERROR/FATAL 直接构造 Logger（无 if），适合对比
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_WARN_INT)(benchmark::State& state)
{
    int i = 0;
    for (auto _ : state)
    {
        LOG_WARN << i++;  // 无 if 过滤，无条件执行
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_WARN_INT)(benchmark::State& state)
// {
//     int i = 0;
//     for (auto _ : state)
//     {
//         FLOG_WARN("{}", i++);
//     }
// }

// ============================================================================
// 10. 格式化精度 —— 对比 format_to_n 直接写入 LogStream vs std::format 中间临时
// string
//     FLOG 中使用了 std::format 到临时 string，这会产生额外的内存分配开销
//     LOG 中直接通过 operator<< + to_chars 写入栈上 FixedBuffer，零分配
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_THREE_INTS)(benchmark::State& state)
{
    int a = 1, b = 2, c = 3;
    for (auto _ : state)
    {
        LOG_INFO << a << " " << b << " " << c;
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_THREE_INTS)(benchmark::State& state)
// {
//     int a = 1, b = 2, c = 3;
//     for (auto _ : state)
//     {
//         FLOG_INFO("{} {} {}", a, b, c);
//     }
// }

// ============================================================================
// 11. 大整数 (int64_t) —— 更多位数需要更多 to_chars / format 工作
// ============================================================================

BENCHMARK_F(LoggingFixture, LOG_INT64)(benchmark::State& state)
{
    int64_t i = INT64_MAX - 100000;
    for (auto _ : state)
    {
        LOG_INFO << i++;
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_INT64)(benchmark::State& state)
// {
//     int64_t i = INT64_MAX - 100000;
//     for (auto _ : state)
//     {
//         FLOG_INFO("{}", i++);
//     }
// }

BENCHMARK_F(LoggingFixture, LOG_FORMAT)(benchmark::State& state)
{
    int64_t i = 0;
    for (auto _ : state)
    {
        LOG_INFO << "This is a format " << i++ << " with " << i * 12.5
                 << " on the floor (Thread: " << &i
                 << ")";
    }
}

// BENCHMARK_F(LoggingFixture, FLOG_FORMAT)(benchmark::State& state)
// {
//     int64_t i = INT64_MAX - 100000;
//     for (auto _ : state)
//     {
//         FLOG_INFO("This is a format {} with {} on the floor (Thread: {})", i,
//                   i * 12.5, (void*)&i);
//     }
// }
