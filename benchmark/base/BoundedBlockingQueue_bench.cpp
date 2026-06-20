// #include "chaoxi/base/BoundedBlockingQueue.hpp"
// #include "chaoxi/base/CurrentThread.hpp"
// #include "chaoxi/base/Logging.hpp"
// #include "chaoxi/base/Timestamp.hpp"

// #include <cassert>
// #include <chrono>
// #include <format>
// #include <latch>
// #include <map>
// #include <memory>
// #include <print>
// #include <thread>
// #include <vector>

// #include <unistd.h>

// // Many threads, one queue.
// class Bench
// {
// public:
//     Bench(int numThreads) : latch_(numThreads)
//     {
//         for (int i = 0; i != numThreads; ++i)
//         {
//             // auto name = std::format("work thread {}", i);
//             threads_.emplace_back([this] { this->threadFunc(); });
//         }
//     }

//     void run(int times)
//     {
//         std::println("waiting for count down latch");

//         latch_.wait();

//         LOG_INFO << threads_.size() << " threads started";

//         std::int64_t total_delay = 0;

//         for (int i = 0; i != times; ++i)
//         {
//             auto now = chaoxi::Timestamp::clock::now();
//             queue_.put(now);
//             total_delay += delay_queue_.take();
//         }
//         std::println("Average delay: {}us",
//                      static_cast<double>(total_delay) / times);
//     }

//     void joinAll()
//     {
//         for (size_t i = 0; i < threads_.size(); ++i)
//         {
//             queue_.put(chaoxi::Timestamp::min());
//         }

//         for (auto& thr : threads_)
//         {
//             if (thr.joinable())
//             {
//                 thr.join();
//             }
//         }
//         LOG_INFO << threads_.size() << " threads stopped";
//     }

//     // void joinAll()

// private:
//     void threadFunc()
//     {
//         // std::println("tid={}. {} started", chaoxi::CurrentThread::tid(),
//                     //  chaoxi::CurrentThread::name());
//         std::map<int, int> delays;

//         latch_.count_down();

//         bool running = true;
//         while (running)
//         {
//             auto t{queue_.take()};
//             auto now = chaoxi::Timestamp::clock::now();
//             if (t != chaoxi::Timestamp::min())
//             {
//                 int delay =
//                     std::chrono::duration_cast<std::chrono::microseconds>(now
//                     -
//                                                                           t)
//                         .count();

//                 ++delays[delay];
//                 delay_queue_.put(delay);
//             }

//             running = (t != chaoxi::Timestamp::min());
//         }

//         // std::println("tid={}. {} stopped", chaoxi::CurrentThread::tid(),
//         //              chaoxi::CurrentThread::name());

//         for (auto [delay, cnt] : delays)
//         {
//             // std::println("tid={}, delay={}, count={}",
//             //              chaoxi::CurrentThread::tid(), delay, cnt);
//         }
//     }

//     chaoxi::BoundedBlockingQueue<chaoxi::Timestamp> queue_;
//     chaoxi::BoundedBlockingQueue<int> delay_queue_;
//     std::latch latch_;
//     std::vector<std::jthread> threads_;
// };

// int main(int argc, char* argv[])
// {
//     int threads = argc > 1 ? atoi(argv[1]) : 1;

//     Bench t(threads);
//     t.run(100000);
//     t.joinAll();
// }

#include "chaoxi/base/BoundedBlockingQueue.hpp"
#include "chaoxi/base/CurrentThread.hpp"
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"

#include <latch>
#include <memory>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

// ============================================================================
// 1. 延迟测试 (Latency Test - Ping-Pong)
// 模拟真实场景下一个线程发送任务，另一个线程处理后返回结果的极限往返时间。
// 使用 Fixture 以避免在每次迭代中重复创建线程。
// ============================================================================
class PingPongFixture : public benchmark::Fixture
{
public:
    chaoxi::BoundedBlockingQueue<int> to_worker{65536};
    chaoxi::BoundedBlockingQueue<int> from_worker{65536};
    std::unique_ptr<std::jthread> worker;

    void SetUp(const ::benchmark::State& state) override
    {
        worker = std::make_unique<std::jthread>(
            [this]
            {
                while (true)
                {
                    int v{};
                    auto res = to_worker.take(v);
                    if (res && v == -1)
                    {
                        break;  // 毒药丸退出机制
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
        int result{};
        auto res = from_worker.take(result);
        benchmark::DoNotOptimize(
            result);  // 防止编译器把没被使用的 result 优化掉
    }
}

// ============================================================================
// 2. 吞吐量与竞争测试 (Throughput & Contention Test)
// 测试在多生产者 (Producers) 和多消费者 (Consumers) 争抢同一队列时的性能。
// 参数 0: 生产者数量 | 参数 1: 消费者数量
// ============================================================================
static void BM_Queue_Throughput(benchmark::State& state)
{
    int num_producers = state.range(0);
    int num_consumers = state.range(1);

    // 每次迭代中每个生产者发送的数量。
    // 不要设得太大，因为 Google Benchmark 本身会跑非常多次迭代。
    const int items_per_producer = 10000;

    for (auto _ : state)
    {
        // 暂停计时：线程的创建和销毁不应计入队列性能的测试时间
        state.PauseTiming();

        chaoxi::BoundedBlockingQueue<int> q{65536};
        std::latch start_latch(num_producers + num_consumers + 1);

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
                        int v{};
                        auto res = q.take(v);
                        if (res && v == -1)
                        {
                            break;  // 毒药丸退出机制
                        }
                        benchmark::DoNotOptimize(v);
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

        // 2. 生产者发完后，向消费者发送毒药丸让它们安全退出
        for (int i = 0; i < num_consumers; ++i)
        {
            q.put(-1);
        }

        // 3. 暂停计时，等待消费者线程安全销毁
        state.PauseTiming();
        for (auto& t : consumers)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        state.ResumeTiming();
    }

    // 统计每秒处理的消息总数 (Items/s)
    state.SetItemsProcessed(state.iterations() * num_producers *
                            items_per_producer);
}

// 注册不同的线程比例进行全面测试
BENCHMARK(BM_Queue_Throughput)
    ->Args({1, 1})    // SPSC: 1个生产者, 1个消费者 (基准吞吐量)
    ->Args({4, 1})    // MPSC: 4个生产者, 1个消费者 (写入竞争)
    ->Args({1, 4})    // SPMC: 1个生产者, 4个消费者 (读取竞争)
    ->Args({4, 4})    // MPMC: 4个生产者, 4个消费者 (全面竞争)
    ->Args({8, 8})    // 高负载: 8个生产者, 8个消费者
    ->UseRealTime();  // 强制使用墙上时钟(Real Time)衡量多线程性能

// //
// ============================================================================
// // 3. 烫手山芋测试 (Hot Potato / Ring Topology Test)
// // 测试极端上下文切换开销、条件变量唤醒延迟以及 CPU Cache 一致性开销。
// // 参数 0: 环中的线程数量
// //
// ============================================================================
static void BM_Queue_HotPotato(benchmark::State& state)
{
    int num_threads = state.range(0);

    // 每一轮传递的总次数（山芋被丢来丢去的次数）
    // 设为固定值，以便计算单次传递(hop)的平均耗时
    const int hops = 100000;

    // 初始化 N 个队列和 1 个完成信号队列
    std::vector<std::shared_ptr<chaoxi::BoundedBlockingQueue<int>>> queues;
    for (int i = 0; i < num_threads; ++i)
    {
        queues.push_back(
            std::make_shared<chaoxi::BoundedBlockingQueue<int>>(65536));
    }
    chaoxi::BoundedBlockingQueue<int> done_queue{65536};

    std::latch start_latch(num_threads + 1);
    std::vector<std::jthread> threads;

    // 构建环形线程拓扑
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(
            [i, num_threads, &queues, &done_queue, &start_latch]
            {
                auto& input = queues[i];
                auto& output = queues[(i + 1) % num_threads];  //

                start_latch.count_down();  // 报告就绪

                while (true)
                {
                    int value{};
                    bool res = input->take(value);
                    if (!res)
                    {
                        continue;
                    }
                    if (res && value > 0)
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

    // === 开始 Google Benchmark 核心循环 ===
    for (auto _ : state)
    {
        // 扔入山芋
        queues[0]->put(hops);

        // 阻塞等待山芋被完全消化
        int done_thread_id{};
        (void)done_queue.take(done_thread_id);
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

    // 统计：框架会自动算出总耗时，这里我们告诉它做了多少次 Context Switch
    // (Hops)
    // 这样控制台会输出一个准确的 Items/s（每秒完成的上下文切换次数）
    state.SetItemsProcessed(state.iterations() * hops);
}

// 注册 Hot Potato 测试：测试 2、4、8、16 个线程组成环的表现
// BENCHMARK(BM_Queue_HotPotato)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->UseRealTime();

// ============================================================================
// Google Benchmark 主函数入口
// ============================================================================
BENCHMARK_MAIN();