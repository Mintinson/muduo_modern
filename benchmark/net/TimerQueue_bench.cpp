#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TimerId.hpp"

#include <cstddef>
#include <vector>

#include <benchmark/benchmark.h>

namespace
{
constexpr double kFarFutureSeconds = 3'600.0;

void addTimers(benchmark::State& state)
{
    const auto timerCount = static_cast<std::size_t>(state.range(0));
    chaoxi::net::EventLoop loop;
    std::vector<chaoxi::net::TimerId> timers;
    timers.reserve(timerCount);

    for (auto _ : state)
    {
        (void)_;
        for (std::size_t index = 0; index < timerCount; ++index)
        {
            timers.push_back(loop.runAfter(kFarFutureSeconds, [] {}));
        }

        state.PauseTiming();
        for (const auto timer : timers)
        {
            loop.cancel(timer);
        }
        timers.clear();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(timerCount));
}

void cancelTimers(benchmark::State& state)
{
    const auto timerCount = static_cast<std::size_t>(state.range(0));
    chaoxi::net::EventLoop loop;
    std::vector<chaoxi::net::TimerId> timers;
    timers.reserve(timerCount);

    for (auto _ : state)
    {
        (void)_;
        state.PauseTiming();
        for (std::size_t index = 0; index < timerCount; ++index)
        {
            timers.push_back(loop.runAfter(kFarFutureSeconds, [] {}));
        }
        state.ResumeTiming();
        for (const auto timer : timers)
        {
            loop.cancel(timer);
        }
        state.PauseTiming();
        timers.clear();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(timerCount));
}

void expireTimers(benchmark::State& state)
{
    const auto timerCount = static_cast<std::size_t>(state.range(0));
    chaoxi::net::EventLoop loop;

    for (auto _ : state)
    {
        (void)_;
        state.PauseTiming();
        std::size_t expired{};
        for (std::size_t index = 0; index < timerCount; ++index)
        {
            loop.runAfter(0.0,
                          [&]
                          {
                              if (++expired == timerCount)
                              {
                                  loop.quit();
                              }
                          });
        }
        state.ResumeTiming();
        loop.loop();
        benchmark::DoNotOptimize(expired);
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(timerCount));
}

void registerTimerSizes(benchmark::internal::Benchmark* benchmark)
{
    benchmark->Arg(1'000)
        ->Arg(100'000)
        ->Arg(1'000'000)
        ->Iterations(1)
        ->UseRealTime();
}

BENCHMARK(addTimers)->Name("TimerQueue/Add")->Apply(registerTimerSizes);
BENCHMARK(cancelTimers)->Name("TimerQueue/Cancel")->Apply(registerTimerSizes);
BENCHMARK(expireTimers)->Name("TimerQueue/Expire")->Apply(registerTimerSizes);
}  // namespace
