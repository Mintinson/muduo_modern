///
/// @file tests/base/AsyncLogging_test.cpp
/// @brief AsyncLogging 单元测试 —— 双缓冲异步日志写入与落盘
///

#include "chaoxi/base/AsyncLogging.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <unistd.h>

using namespace chaoxi;

class AsyncLoggingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 使用唯一前缀, 方便测试后清理
        basename_ = "async_logging_test" + std::to_string(getpid());
        // 清理可能残留的日志文件 (在当前工作目录)
        cleanupLogFiles();
    }

    void TearDown() override { cleanupLogFiles(); }

    /// 删除所有匹配 basename_ 开头的日志文件
    void cleanupLogFiles()
    {
        for (const auto& entry : std::filesystem::directory_iterator(
                 std::filesystem::current_path()))
        {
            auto name = entry.path().filename().string();
            if (name.starts_with(basename_))
            {
                std::filesystem::remove(entry.path());
            }
        }
    }

    /// 读取所有匹配 basename_ 的日志文件内容（合并）
    std::string readAllLogFiles()
    {
        std::string result;
        for (const auto& entry : std::filesystem::directory_iterator(
                 std::filesystem::current_path()))
        {
            auto name = entry.path().filename().string();
            if (name.starts_with(basename_))
            {
                std::ifstream in(entry.path());
                result.append(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
            }
        }
        return result;
    }

    /// 统计匹配 basename_ 的日志文件数量
    int countLogFiles()
    {
        int count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(
                 std::filesystem::current_path()))
        {
            if (entry.path().filename().string().starts_with(basename_))
            {
                ++count;
            }
        }
        return count;
    }

    std::string basename_;
};

// ============================================================================
// 测试1: 构造和析构（不调用 start）
// ============================================================================

TEST_F(AsyncLoggingTest, ConstructAndDestruct)
{
    {
        AsyncLogging log(basename_, 500 * 1000);
    }
    SUCCEED();
}

// ============================================================================
// 测试2: 单线程写 → stop → 数据正确落盘
// ============================================================================

TEST_F(AsyncLoggingTest, SingleThreadWrite)
{
    // flushInterval=1s: 后端每1秒醒来处理一次
    AsyncLogging log(basename_, 500 * 1000, 1);
    log.start();

    for (int i = 0; i < 100; ++i)
    {
        log.append("line " + std::to_string(i) + "\n");
    }

    // 等待后端线程被 flushInterval 唤醒并处理数据
    std::this_thread::sleep_for(std::chrono::seconds(2));
    log.stop();

    std::string content = readAllLogFiles();
    EXPECT_NE(content.find("line 0"), std::string::npos);
    EXPECT_NE(content.find("line 99"), std::string::npos);
    EXPECT_GE(countLogFiles(), 1);
}

// ============================================================================
// 测试3: 大量数据, 验证 buffer 队列 + 积压不 crash
// ============================================================================

TEST_F(AsyncLoggingTest, LargeVolumeWrite)
{
    AsyncLogging log(basename_, 500 * 1000, 1);
    log.start();

    constexpr int kIterations = 2000;
    for (int i = 0; i < kIterations; ++i)
    {
        log.append(std::string(1000, static_cast<char>('A' + (i % 26))) + "\n");
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    log.stop();

    std::string content = readAllLogFiles();
    EXPECT_GT(content.size(), kIterations * 1000ull);
}

// ============================================================================
// 测试4: 4 个线程并发写入
// ============================================================================

TEST_F(AsyncLoggingTest, MultiThreadWrite)
{
    AsyncLogging log(basename_, 500 * 1000, 1);
    log.start();

    constexpr int kNumThreads = 4;
    constexpr int kLinesPerThread = 250;
    std::vector<std::jthread> threads;

    for (int t = 0; t < kNumThreads; ++t)
    {
        threads.emplace_back(
            [&log, t]
            {
                for (int i = 0; i < kLinesPerThread; ++i)
                {
                    log.append("t" + std::to_string(t) + "_line" +
                               std::to_string(i) + "\n");
                }
            });
    }
    threads.clear();  // jthread 析构自动 join

    std::this_thread::sleep_for(std::chrono::seconds(2));
    log.stop();

    std::string content = readAllLogFiles();
    for (int t = 0; t < kNumThreads; ++t)
    {
        EXPECT_NE(content.find("t" + std::to_string(t) + "_line0"),
                  std::string::npos);
    }
}

// ============================================================================
// 测试5: stop 后 append 不 crash
// ============================================================================

TEST_F(AsyncLoggingTest, StopThenAppendNoCrash)
{
    AsyncLogging log(basename_, 500 * 1000, 1);
    log.start();
    log.append("before stop\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    log.stop();
    log.append("after stop\n");  // 不应 crash
    SUCCEED();
}

// ============================================================================
// 测试6: 小 rollSize → 触发日志滚动
// ============================================================================

TEST_F(AsyncLoggingTest, LogFileRolling)
{
    // rollSize=100 字节, 每条日志 >200 字节, 在 LogFile 层面触发多次滚动
    AsyncLogging log(basename_, 100, 1);
    log.start();

    for (int i = 0; i < 50; ++i)
    {
        log.append(std::string(200, static_cast<char>('X' + (i % 3))) + "\n");
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    log.stop();

    int count = countLogFiles();
    EXPECT_GE(count, 2) << "Expected at least 2 log files after rolling, got "
                        << count;
}

// ============================================================================
// 测试7: 空消息不 crash
// ============================================================================

TEST_F(AsyncLoggingTest, EmptyAppend)
{
    AsyncLogging log(basename_, 500 * 1000, 1);
    log.start();
    log.append("");
    log.append(std::string_view{});
    std::this_thread::sleep_for(std::chrono::seconds(2));
    log.stop();
    SUCCEED();
}

// ============================================================================
// 测试8: flushInterval 触发定期刷盘
// ============================================================================

TEST_F(AsyncLoggingTest, FlushIntervalTriggersFlush)
{
    AsyncLogging log(basename_, 500 * 1000, 1);  // 1 秒刷一次
    log.start();

    log.append("important message\n");

    // 等 2 秒让 flushInterval 超时触发
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 不 stop, 直接读文件——flush 应该已把数据写入
    std::string content = readAllLogFiles();
    EXPECT_NE(content.find("important message"), std::string::npos);

    log.stop();
}
