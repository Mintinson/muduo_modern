#include "chaoxi/base/BlockingQueue.hpp"
#include "chaoxi/base/CurrentThread.hpp"
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <latch>
#include <memory>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

// ============================================================================
// 1. 往返延迟测试
// 模拟真实场景下一个线程发送任务，另一个线程处理后返回结果的极限往返时间。
// 使用测试夹具，避免每次迭代都重复创建线程。
// ============================================================================
class PingPongFixture : public benchmark::Fixture
{
public:
    chaoxi::BlockingQueue<int> to_worker;
    chaoxi::BlockingQueue<int> from_worker;
    std::unique_ptr<std::jthread> worker;

    void SetUp(const ::benchmark::State& state) override
    {
        worker = std::make_unique<std::jthread>(
            [this]
            {
                while (true)
                {
                    int v = to_worker.take();
                    if (v == -1)
                    {
                        break;  // 通过毒药丸安全退出。
                    }
                    from_worker.put(v);
                }
            });
    }

    void TearDown(const ::benchmark::State& state) override
    {
        to_worker.put(-1);
        if (worker->joinable())
        {
            worker->join();
        }
    }
};

BENCHMARK_F(PingPongFixture, Latency_PingPong)(benchmark::State& state)
{
    for (auto _ : state)
    {
        // 测算单次数据放进队列并被取回的完整周期时间
        to_worker.put(1);
        int result = from_worker.take();
        benchmark::DoNotOptimize(
            result);  // 防止编译器把没被使用的 result 优化掉
    }
}

// ============================================================================
// 2. 吞吐量与竞争测试
// 测试多生产者和多消费者争用同一队列时的性能。
// 参数 0: 生产者数量 | 参数 1: 消费者数量
// ============================================================================
static void BM_Queue_Throughput(benchmark::State& state)
{
    int num_producers = state.range(0);
    int num_consumers = state.range(1);

    // 每次迭代中每个生产者发送的数量。
    // 不宜设置过大，因为基准框架本身会执行很多轮迭代。
    const int items_per_producer = 10000;

    for (auto _ : state)
    {
        // 暂停计时：线程的创建和销毁不应计入队列性能的测试时间
        state.PauseTiming();

        chaoxi::BlockingQueue<int> q;
        std::latch start_latch(num_producers + num_consumers + 1);
        std::latch consumed_latch(num_producers * items_per_producer);

        std::vector<std::jthread> producers;
        for (int i = 0; i < num_producers; ++i)
        {
            producers.emplace_back(
                [&]
                {
                    start_latch.arrive_and_wait();  // 等待所有线程就绪
                    for (int j = 0; j < items_per_producer; ++j)
                    {
                        q.put(1);
                    }
                });
        }

        std::vector<std::jthread> consumers;
        for (int i = 0; i < num_consumers; ++i)
        {
            consumers.emplace_back(
                [&]
                {
                    start_latch.arrive_and_wait();  // 等待所有线程就绪
                    while (true)
                    {
                        int v = q.take();
                        if (v == -1)
                        {
                            break;  // 毒药丸退出机制
                        }
                        benchmark::DoNotOptimize(v);
                        consumed_latch.count_down();
                    }
                });
        }

        // 恢复计时，并触发所有线程同时开始工作
        state.ResumeTiming();
        start_latch.arrive_and_wait();

        // 1. 等待所有生产者完成推送
        for (auto& t : producers)
        {
            if (t.joinable())
            {
                t.join();
            }
        }

        // 吞吐计时必须覆盖最后一条业务消息被消费者取走，而不仅是生产者完成。
        consumed_latch.wait();

        // 停机协议和线程销毁不属于队列业务吞吐，移出计时区间。
        state.PauseTiming();

        // 2. 生产者发完后，向消费者发送毒药丸让它们安全退出
        for (int i = 0; i < num_consumers; ++i)
        {
            q.put(-1);
        }

        // 3. 等待消费者线程安全销毁
        for (auto& t : consumers)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        state.ResumeTiming();
    }

    // 统计每秒处理的消息总数。
    state.SetItemsProcessed(state.iterations() * num_producers *
                            items_per_producer);
}

// 注册不同的线程比例进行全面测试
BENCHMARK(BM_Queue_Throughput)
    ->Args({1, 1})    // 单生产者、单消费者：基准吞吐量。
    ->Args({4, 1})    // 多生产者、单消费者：写入竞争。
    ->Args({1, 4})    // 单生产者、多消费者：读取竞争。
    ->Args({4, 4})    // 多生产者、多消费者：全面竞争。
    ->Args({8, 8})    // 高负载：8 个生产者和 8 个消费者。
    ->UseRealTime();  // 多线程性能必须使用墙钟时间衡量。

// ============================================================================
// 3. 逐消息排队延迟分布
// ============================================================================
struct TimedItem
{
    std::chrono::steady_clock::time_point enqueuedAt;
    bool stop{};
};

[[nodiscard]] double percentile(const std::vector<std::int64_t>& sorted,
                                double quantile)
{
    const auto rank = quantile * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(rank);
    const auto upper = std::min(lower + 1, sorted.size() - 1);
    const auto fraction = rank - static_cast<double>(lower);
    return static_cast<double>(sorted[lower]) +
           static_cast<double>(sorted[upper] - sorted[lower]) * fraction;
}

static void BM_Queue_LatencyDistribution(benchmark::State& state)
{
    const auto producerCount = static_cast<std::size_t>(state.range(0));
    const auto consumerCount = static_cast<std::size_t>(state.range(1));
    constexpr std::size_t kSamplesPerProducer = 25'000;
    const auto totalSamples = producerCount * kSamplesPerProducer;

    for (auto _ : state)
    {
        chaoxi::BlockingQueue<TimedItem> queue;
        std::latch ready{
            static_cast<std::ptrdiff_t>(producerCount + consumerCount)};
        std::latch start{1};
        std::vector<std::vector<std::int64_t>> samples(consumerCount);
        std::vector<std::jthread> consumers;
        std::vector<std::jthread> producers;
        consumers.reserve(consumerCount);
        producers.reserve(producerCount);

        for (std::size_t consumer = 0; consumer < consumerCount; ++consumer)
        {
            samples[consumer].reserve(totalSamples / consumerCount + 1);
            consumers.emplace_back(
                [&, consumer]
                {
                    ready.count_down();
                    start.wait();
                    while (true)
                    {
                        auto item = queue.take();
                        if (item.stop)
                        {
                            break;
                        }
                        const auto latency =
                            std::chrono::steady_clock::now() - item.enqueuedAt;
                        samples[consumer].push_back(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                latency)
                                .count());
                    }
                });
        }
        for (std::size_t producer = 0; producer < producerCount; ++producer)
        {
            producers.emplace_back(
                [&]
                {
                    ready.count_down();
                    start.wait();
                    for (std::size_t sample = 0; sample < kSamplesPerProducer;
                         ++sample)
                    {
                        queue.put(
                            {.enqueuedAt = std::chrono::steady_clock::now()});
                    }
                });
        }

        ready.wait();
        start.count_down();
        producers.clear();
        for (std::size_t consumer = 0; consumer < consumerCount; ++consumer)
        {
            queue.put({.stop = true});
        }
        consumers.clear();

        std::vector<std::int64_t> merged;
        merged.reserve(totalSamples);
        for (auto& consumerSamples : samples)
        {
            merged.insert(merged.end(), consumerSamples.begin(),
                          consumerSamples.end());
        }
        if (merged.size() != totalSamples)
        {
            state.SkipWithError("延迟采样器丢失了消息");
            break;
        }
        std::ranges::sort(merged);
        state.counters["latency_p50_ns"] = percentile(merged, 0.50);
        state.counters["latency_p90_ns"] = percentile(merged, 0.90);
        state.counters["latency_p99_ns"] = percentile(merged, 0.99);
        state.counters["latency_p99_9_ns"] = percentile(merged, 0.999);
        state.counters["latency_max_ns"] = static_cast<double>(merged.back());
        state.counters["latency_samples"] = static_cast<double>(merged.size());
    }

    state.SetItemsProcessed(
        static_cast<std::int64_t>(state.iterations() * totalSamples));
}

BENCHMARK(BM_Queue_LatencyDistribution)
    ->ArgsProduct({
        {1, 4},
        {1, 4}
})
    ->Iterations(1)
    ->UseRealTime();

// ============================================================================
// 4. 环形传递测试
// 测试极端上下文切换、条件变量唤醒和 CPU 缓存一致性开销。
// 参数 0: 环中的线程数量
// ============================================================================
static void BM_Queue_HotPotato(benchmark::State& state)
{
    int num_threads = state.range(0);

    // 每一轮传递的总次数（山芋被丢来丢去的次数）
    // 设为固定值，以便计算单次传递的平均耗时。
    const int hops = 100000;

    // 初始化 N 个工作队列和一个完成信号队列。
    std::vector<std::shared_ptr<chaoxi::BlockingQueue<int>>> queues;
    for (int i = 0; i < num_threads; ++i)
    {
        queues.push_back(std::make_shared<chaoxi::BlockingQueue<int>>());
    }
    chaoxi::BlockingQueue<int> done_queue;

    std::latch start_latch(num_threads + 1);
    std::vector<std::jthread> threads;

    // 构建环形线程拓扑
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(
            [i, num_threads, &queues, &done_queue, &start_latch]
            {
                auto& input = queues[i];
                auto& output = queues[(i + 1) % num_threads];  // 指向下一个队列

                start_latch.count_down();  // 报告就绪

                while (true)
                {
                    int value = input->take();

                    if (value > 0)
                    {
                        // 山芋还有温度，减1传给下一个人
                        output->put(value - 1);
                    }
                    else if (value == 0)
                    {
                        // 山芋凉了(归零)，通知主线程本轮测试结束
                        done_queue.put(i);
                    }
                    else
                    {
                        // 收到毒药丸 (-1)，退出线程
                        break;
                    }
                }
            });
    }

    // 等待所有工作线程就绪，确保不在测试计时期间发生线程创建开销
    start_latch.arrive_and_wait();

    // === 开始计时框架的核心循环 ===
    for (auto _ : state)
    {
        // 扔入山芋
        queues[0]->put(hops);

        // 阻塞等待山芋被完全消化
        int done_thread_id = done_queue.take();
        benchmark::DoNotOptimize(done_thread_id);
    }
    // === 结束核心循环 ===

    // 优雅停机：向所有队列发送毒药丸
    for (int i = 0; i < num_threads; ++i)
    {
        queues[i]->put(-1);
    }
    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    // 告诉框架实际完成的传递次数，使其输出每秒传递次数。
    state.SetItemsProcessed(state.iterations() * hops);
}

// 测试由 2、4、8、16 个线程组成的环形拓扑。
BENCHMARK(BM_Queue_HotPotato)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->UseRealTime();

// ============================================================================
// 基准测试主函数入口。
// ============================================================================
BENCHMARK_MAIN();
