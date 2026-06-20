#include "chaoxi/base/AsyncLogging.hpp"
#include "chaoxi/base/Logging.hpp"

#include <benchmark/benchmark.h>
#include <string>

using namespace chaoxi;

// ============================================================================
// 全局桥接：让 Logger 的输出重定向到我们的 AsyncLogging 实例
// ============================================================================
static AsyncLogging* g_asyncLog = nullptr;

void asyncOutput(std::string_view msg)
{
    if (g_asyncLog)
    {
        g_asyncLog->append(msg);
    }
}

void asyncFlush() {} // 异步日志不需要同步 flush

// ============================================================================
// 测试夹具 (Fixture)：管理 AsyncLogging 的生命周期
// ============================================================================
class AsyncLoggingFixture : public benchmark::Fixture
{
public:
    void SetUp(benchmark::State& state) override
    {
        // 保证多线程测试时，只有第一个线程去初始化后端
        if (state.thread_index() == 0)
        {
            // 滚动大小设为 1GB，避免压测期间频繁切文件
            asyncLog_ = std::make_unique<AsyncLogging>("/tmp/async_logging_bench", 1024 * 1024 * 1024);
            asyncLog_->start();
            g_asyncLog = asyncLog_.get();
            Logger::setOutput(asyncOutput);
            Logger::setFlush(asyncFlush);
        }
    }

    void TearDown(benchmark::State& state) override
    {
        // 保证所有线程跑完后，由最后一个退出的线程负责停止和清理
        if (state.thread_index() == 0)
        {
            g_asyncLog = nullptr;
            asyncLog_->stop(); // 等待后端将队列中的数据全部落盘
            asyncLog_.reset();
        }
    }

    static std::unique_ptr<AsyncLogging> asyncLog_;
};

std::unique_ptr<AsyncLogging> AsyncLoggingFixture::asyncLog_ = nullptr;

// ============================================================================
// Benchmark: 单线程异步写入
// ============================================================================
BENCHMARK_F(AsyncLoggingFixture, SingleThread)(benchmark::State& state)
{
    // 构造一条长约 100 字节的消息
    const char* msg = "This is a standard log message meant to simulate typical business logic output length. 1234567890";
    
    for (auto _ : state)
    {
        LOG_INFO << msg;
    }

    // 统计每秒处理的消息数和吞吐量 (加上时间戳等 header 大约 140 字节)
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * 140);
}

// ============================================================================
// Benchmark: 多线程高并发异步写入 (测试锁竞争)
// ============================================================================
BENCHMARK_F(AsyncLoggingFixture, MultiThread)(benchmark::State& state)
{
    const char* msg = "This is a standard log message meant to simulate typical business logic output length. 1234567890";
    
    for (auto _ : state)
    {
        LOG_INFO << msg;
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * 140);
}

// 模拟 4 个和 8 个业务线程同时写日志的场景
BENCHMARK_REGISTER_F(AsyncLoggingFixture, MultiThread)->Threads(4);
BENCHMARK_REGISTER_F(AsyncLoggingFixture, MultiThread)->Threads(8);