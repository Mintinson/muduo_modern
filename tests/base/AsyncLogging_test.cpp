///
/// @file tests/base/AsyncLogging_test.cpp
/// @brief AsyncLogging 单元测试 —— 双缓冲异步日志写入与落盘
///

#include "chaoxi/base/AsyncLogging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

using namespace chaoxi;

class AsyncLoggingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 使用唯一前缀, 方便测试后清理
        basename_ = "async_logging_test" + std::to_string(process_info::pid());
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
// 测试 1：构造和析构（不调用 start）。
// ============================================================================

TEST_F(AsyncLoggingTest, ConstructAndDestruct)
{
    {
        AsyncLogging log(basename_, 500 * 1000);
    }
    SUCCEED();
}

// ============================================================================
// 测试 2：单线程写入后停止，数据应正确落盘。
// ============================================================================

TEST_F(AsyncLoggingTest, SingleThreadWrite)
{
    // 刷新间隔为 1 秒：后端每秒醒来处理一次。
    AsyncLogging log(basename_, 500 * 1000, 1);
    log.start();

    for (int i = 0; i < 100; ++i)
    {
        log.append("line " + std::to_string(i) + "\n");
    }

    // 等待后端线程被定期刷新机制唤醒并处理数据。
    std::this_thread::sleep_for(std::chrono::seconds(2));
    log.stop();

    std::string content = readAllLogFiles();
    EXPECT_NE(content.find("line 0"), std::string::npos);
    EXPECT_NE(content.find("line 99"), std::string::npos);
    EXPECT_GE(countLogFiles(), 1);
}

TEST_F(AsyncLoggingTest, StopDrainsCurrentBufferWithoutWaitingForFlushInterval)
{
    AsyncLogging log(basename_, 500 * 1000, 60);
    log.start();
    log.append("tail that must survive stop\n");
    log.stop();

    EXPECT_EQ(readAllLogFiles(), "tail that must survive stop\n");
    const auto statistics = log.statistics();
    EXPECT_EQ(statistics.acceptedMessages, 1);
    EXPECT_EQ(statistics.acceptedBytes, 28);
    EXPECT_EQ(statistics.writtenMessages, statistics.acceptedMessages);
    EXPECT_EQ(statistics.writtenBytes, statistics.acceptedBytes);
    EXPECT_EQ(statistics.droppedMessages, 0);
    EXPECT_EQ(statistics.droppedBytes, 0);
}

// ============================================================================
// 测试 3：大量数据，验证缓冲队列积压时不会崩溃。
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
    threads.clear();  // jthread 析构时会自动等待线程退出。

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
// 测试 5：停止后继续追加不会崩溃，并会计入丢弃统计。
// ============================================================================

TEST_F(AsyncLoggingTest, StopThenAppendNoCrash)
{
    AsyncLogging log(basename_, 500 * 1000, 1);
    log.start();
    log.append("before stop\n");
    log.stop();
    log.append("after stop\n");

    const auto statistics = log.statistics();
    EXPECT_EQ(statistics.acceptedMessages, 1);
    EXPECT_EQ(statistics.writtenMessages, 1);
    EXPECT_EQ(statistics.droppedMessages, 1);
    EXPECT_EQ(statistics.droppedBytes, std::string_view("after stop\n").size());
}

TEST_F(AsyncLoggingTest, OversizeMessageIsCountedAsDropped)
{
    AsyncLogging log(basename_, 500 * 1000, 60);
    log.start();
    const std::string message(AsyncLogging::kDefaultBufferSize, 'x');
    log.append(message);
    log.stop();

    const auto statistics = log.statistics();
    EXPECT_EQ(statistics.acceptedMessages, 0);
    EXPECT_EQ(statistics.writtenMessages, 0);
    EXPECT_EQ(statistics.droppedMessages, 1);
    EXPECT_EQ(statistics.droppedBytes, message.size());
    EXPECT_TRUE(readAllLogFiles().empty());
}

// ============================================================================
// 测试 6：使用较小滚动阈值触发日志文件滚动。
// ============================================================================

TEST_F(AsyncLoggingTest, LogFileRolling)
{
    // 滚动阈值为 100 字节，每条日志超过 200 字节，会触发多次滚动。
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
// 测试 7：追加空消息不会崩溃。
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
// 测试 8：刷新间隔能够触发定期刷盘。
// ============================================================================

TEST_F(AsyncLoggingTest, FlushIntervalTriggersFlush)
{
    AsyncLogging log(basename_, 500 * 1000, 1);  // 1 秒刷一次
    log.start();

    log.append("important message\n");

    // 等待 2 秒，使刷新间隔超时。
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 不停止日志线程而直接读文件；定期刷新应已把数据写入文件。
    std::string content = readAllLogFiles();
    EXPECT_NE(content.find("important message"), std::string::npos);

    log.stop();
}
