#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThread.hpp"

#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

namespace
{
constexpr std::ptrdiff_t kTasksPerRun = 65'536;

void queueFromProducers(benchmark::State& state)
{
    const auto producerCount = static_cast<std::ptrdiff_t>(state.range(0));
    const auto tasksPerProducer = kTasksPerRun / producerCount;
    chaoxi::net::EventLoopThread loopThread;
    auto* const loop = loopThread.startLoop();

    for (auto _ : state)
    {
        (void)_;
        state.PauseTiming();
        std::latch completed{tasksPerProducer * producerCount};
        std::barrier startGate{producerCount + 1};
        std::vector<std::jthread> producers;
        producers.reserve(static_cast<std::size_t>(producerCount));
        for (std::ptrdiff_t producer = 0; producer < producerCount; ++producer)
        {
            producers.emplace_back(
                [loop, tasksPerProducer, &completed, &startGate]
                {
                    startGate.arrive_and_wait();
                    for (std::ptrdiff_t task = 0; task < tasksPerProducer;
                         ++task)
                    {
                        loop->queueInLoop([&completed]
                                          { completed.count_down(); });
                    }
                });
        }

        state.ResumeTiming();
        startGate.arrive_and_wait();
        completed.wait();
        state.PauseTiming();
        producers.clear();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * kTasksPerRun);
}

BENCHMARK(queueFromProducers)
    ->Name("EventLoop/QueueFromProducers")
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Iterations(1)
    // ->Repetitions(5)                     // 独立跑 5 轮
    // ->ReportAggregatesOnly(true)
    ->UseRealTime();
}  // namespace
