///
/// @file Logging_throughput_bench.cpp
/// @brief 测试 Logger 端到端吞吐量 (Msg/s & MiB/s)
///

#include "chaoxi/base/LogFile.hpp"
#include "chaoxi/base/Logging.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

using namespace chaoxi;

// 全局变量用于统计写入的总字节数
static uint64_t g_totalBytes = 0;
static int g_nullFd = -1;
static std::unique_ptr<LogFile> g_logFile;

// ============================================================================
// 1. Nop Output (纯 CPU 格式化，丢弃数据)
// ============================================================================
void nopOutput(std::string_view msg)
{
    g_totalBytes += msg.size();
}

// ============================================================================
// 2. /dev/null Output (加上系统调用的开销)
// ============================================================================
void nullOutput(std::string_view msg)
{
    g_totalBytes += msg.size();
    if (g_nullFd >= 0)
    {
        ::write(g_nullFd, msg.data(), msg.size());
    }
}

// ============================================================================
// 3. File Output (实际落盘，通过 LogFile)
// ============================================================================
void fileOutput(std::string_view msg)
{
    g_totalBytes += msg.size();
    if (g_logFile)
    {
        // 你的 LogFile 默认构造是 threadSafe=true，为了测单线程极限可以设为
        // false
        g_logFile->append(msg);
    }
}

// 统一的占位 Flush 函数
void dummyFlush() {}

// ============================================================================
// 测试执行引擎
// ============================================================================
void bench(const char* type, Logger::OutputFunc outputFunc)
{
    Logger::setOutput(outputFunc);
    Logger::setFlush(dummyFlush);
    g_totalBytes = 0;

    const int kBatchSize = 1000'000;  // 每次测试 100 万条日志

    // 构造一条日志负载，使其最终生成的长度接近 110 字节
    // Header 约占 40-50 字节 (日期, 线程id, 级别, 源文件)
    // 加上 msg 约 60 字节，正好达到 110 字节左右
    constexpr std::string_view msg =
        "123456789012345678901234567890123456789012345678901234567890";

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < kBatchSize; ++i)
    {
        LOG_INFO << msg << ' ' << i;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    double seconds = diff.count();

    // 计算吞吐量
    double msg_per_sec = kBatchSize / seconds;
    double bytes_per_sec = g_totalBytes / seconds;
    double mib_per_sec = bytes_per_sec / (1024.0 * 1024.0);

    std::printf("%-15s | %7.1f w | %7.1f MiB/s | Avg Len: %zu bytes\n", type,
                msg_per_sec / 10000.0,  // 转换为 "万(w)/秒"
                mib_per_sec, g_totalBytes / kBatchSize);
}

int main()
{
    std::printf(
        "==============================================================\n");
    std::printf("Target          | Messages/s | Bandwidth   | Msg Info\n");
    std::printf(
        "--------------------------------------------------------------\n");

    // 1. 测试纯格式化开销
    bench("nop", nopOutput);

    // 2. 测试 /dev/null
    g_nullFd = ::open("/dev/null", O_WRONLY);
    if (g_nullFd >= 0)
    {
        bench("/dev/null", nullOutput);
        ::close(g_nullFd);
    }

    // 3. 测试文件 IO (使用你编写的 LogFile，关闭 threadSafe 测试单线程极限)
    // rollSize 设为 1GB 避免在测试中途发生滚动影响成绩
    g_logFile =
        std::make_unique<LogFile>("/tmp/log_bench", 1024 * 1024 * 1024, false);
    bench("/tmp/log", fileOutput);

    std::printf(
        "==============================================================\n");
    return 0;
}