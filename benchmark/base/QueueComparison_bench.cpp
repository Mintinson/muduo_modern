#include "chaoxi/base/BlockingQueue.hpp"
#include "chaoxi/base/BoundedBlockingQueue.hpp"

#include <cstddef>
#include <latch>
#include <thread>
#include <type_traits>

#include <benchmark/benchmark.h>

namespace
{

using MutexQueue = chaoxi::BlockingQueue<int>;
using LockFreeQueue = chaoxi::BoundedBlockingQueue<int>;

template <typename Queue>
void put(Queue& queue, int value)
{
    if constexpr (std::is_same_v<Queue, LockFreeQueue>)
    {
        while (!queue.put(value))
        {
            std::this_thread::yield();
        }
    }
    else
    {
        queue.put(value);
    }
}

template <typename Queue>
int take(Queue& queue)
{
    if constexpr (std::is_same_v<Queue, LockFreeQueue>)
    {
        int value = 0;
        while (!queue.take(value))
        {
            std::this_thread::yield();
        }
        return value;
    }
    else
    {
        return queue.take();
    }
}

static void BM_BlockingQueue_SingleThreadRoundTrip(benchmark::State& state)
{
    MutexQueue queue;
    int value = 0;

    for (auto _ : state)
    {
        queue.put(++value);
        benchmark::DoNotOptimize(queue.take());
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_BlockingQueue_SingleThreadRoundTrip);

static void BM_BoundedQueue_SingleThreadRoundTrip(benchmark::State& state)
{
    LockFreeQueue queue(1'024);
    int value = 0;

    for (auto _ : state)
    {
        benchmark::DoNotOptimize(queue.put(++value));
        int result = 0;
        benchmark::DoNotOptimize(queue.take(result));
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_BoundedQueue_SingleThreadRoundTrip);

template <typename Queue>
void SpscThroughput(benchmark::State& state)
{
    const auto itemCount = static_cast<std::size_t>(state.range(0));

    for (auto _ : state)
    {
        state.PauseTiming();
        std::latch start{3};
        Queue queue = [&]()
        {
            if constexpr (std::is_same_v<Queue, LockFreeQueue>)
            {
                return Queue(1'024);
            }
            else
            {
                return Queue();
            }
        }();
        std::size_t checksum = 0;

        std::jthread producer(
            [&]
            {
                start.arrive_and_wait();
                for (std::size_t value = 1; value <= itemCount; ++value)
                {
                    put(queue, static_cast<int>(value));
                }
            });
        std::jthread consumer(
            [&]
            {
                start.arrive_and_wait();
                for (std::size_t i = 0; i < itemCount; ++i)
                {
                    checksum += static_cast<std::size_t>(take(queue));
                }
            });

        state.ResumeTiming();
        start.arrive_and_wait();
        producer.join();
        consumer.join();
        const std::size_t expected = itemCount * (itemCount + 1) / 2;
        if (checksum != expected)
        {
            state.SkipWithError("queue lost or corrupted an item");
            break;
        }
        benchmark::DoNotOptimize(checksum);
    }

    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(itemCount));
}

static void BM_BlockingQueue_SpscThroughput(benchmark::State& state)
{
    SpscThroughput<MutexQueue>(state);
}

BENCHMARK(BM_BlockingQueue_SpscThroughput)
    ->Arg(1'024)
    ->Arg(16'384)
    ->UseRealTime();

static void BM_BoundedQueue_SpscThroughput(benchmark::State& state)
{
    SpscThroughput<LockFreeQueue>(state);
}

BENCHMARK(BM_BoundedQueue_SpscThroughput)
    ->Arg(1'024)
    ->Arg(16'384)
    ->UseRealTime();

}  // namespace
