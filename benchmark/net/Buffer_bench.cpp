#include "chaoxi/net/Buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include <benchmark/benchmark.h>

namespace
{

static void BM_Buffer_AppendAndRetrieve(benchmark::State& state)
{
    const auto size = static_cast<std::size_t>(state.range(0));
    const std::string payload(size, 'x');
    chaoxi::net::Buffer buffer;

    for (auto _ : state)
    {
        buffer.append(payload);
        benchmark::DoNotOptimize(buffer.peek());
        buffer.retrieveAll();
    }

    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(size));
}

BENCHMARK(BM_Buffer_AppendAndRetrieve)
    ->Arg(64)
    ->Arg(1'024)
    ->Arg(16'384)
    ->Arg(65'536);

static void BM_Buffer_AppendWithGrowth(benchmark::State& state)
{
    const auto size = static_cast<std::size_t>(state.range(0));
    const std::string payload(size, 'x');

    for (auto _ : state)
    {
        chaoxi::net::Buffer buffer(16);
        buffer.append(payload);
        benchmark::DoNotOptimize(buffer.peek());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(size));
}

BENCHMARK(BM_Buffer_AppendWithGrowth)->Arg(1'024)->Arg(16'384)->Arg(65'536);

static void BM_Buffer_IntegerRoundTrip(benchmark::State& state)
{
    chaoxi::net::Buffer buffer;
    std::uint64_t value = 0;

    for (auto _ : state)
    {
        buffer.appendInt(++value);
        benchmark::DoNotOptimize(buffer);
        benchmark::ClobberMemory();
        benchmark::DoNotOptimize(buffer.readInt<std::uint64_t>());
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(sizeof(value)));
}

BENCHMARK(BM_Buffer_IntegerRoundTrip);

static void BM_Buffer_FindCRLF(benchmark::State& state)
{
    const auto lineSize = static_cast<std::size_t>(state.range(0));
    std::string line(lineSize, 'x');
    line += "\r\n";

    chaoxi::net::Buffer buffer;
    buffer.append(line);

    for (auto _ : state)
    {
        benchmark::DoNotOptimize(buffer.findCRLF());
    }

    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(line.size()));
}

BENCHMARK(BM_Buffer_FindCRLF)->Arg(64)->Arg(1'024)->Arg(16'384);

}  // namespace
