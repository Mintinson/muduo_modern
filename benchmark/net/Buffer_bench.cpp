#include "chaoxi/net/Buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#ifndef _WIN32
    #include <sys/socket.h>
    #include <unistd.h>
#endif

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

static void BM_Buffer_AppendWithInternalMove(benchmark::State& state)
{
    const auto size = static_cast<std::size_t>(state.range(0));
    const std::string payload(size, 'x');

    for (auto _ : state)
    {
        state.PauseTiming();
        chaoxi::net::Buffer buffer(size + 64);
        buffer.append(std::string(size, 'p'));
        buffer.retrieve(size - 64);
        state.ResumeTiming();

        // There is enough total capacity, but not enough contiguous writable
        // space. append() must compact the 64 readable bytes first.
        buffer.append(payload);
        benchmark::DoNotOptimize(buffer.peek());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(size));
}

BENCHMARK(BM_Buffer_AppendWithInternalMove)
    ->Arg(1'024)
    ->Arg(16'384)
    ->Arg(65'536);

#ifndef _WIN32
static void BM_Buffer_SocketPairRead(benchmark::State& state)
{
    const auto size = static_cast<std::size_t>(state.range(0));
    const std::string payload(size, 'x');
    int sockets[2]{};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
    {
        state.SkipWithError("socketpair failed");
        return;
    }

    for (auto _ : state)
    {
        state.PauseTiming();
        std::size_t sent = 0;
        while (sent < payload.size())
        {
            const auto n = ::send(sockets[0], payload.data() + sent,
                                  payload.size() - sent, 0);
            if (n <= 0)
            {
                state.SkipWithError("socketpair write failed");
                break;
            }
            sent += static_cast<std::size_t>(n);
        }
        chaoxi::net::Buffer buffer;
        state.ResumeTiming();

        int savedErrno = 0;
        const auto received = buffer.readFd(sockets[1], &savedErrno);
        benchmark::DoNotOptimize(buffer.peek());

        if (received != static_cast<decltype(received)>(size))
        {
            state.SkipWithError("short socketpair read");
            break;
        }
    }

    ::close(sockets[0]);
    ::close(sockets[1]);
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(size));
}

BENCHMARK(BM_Buffer_SocketPairRead)
    ->Arg(64)
    ->Arg(1'024)
    ->Arg(16'384)
    ->Arg(65'536)
    ->UseRealTime();
#endif

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
