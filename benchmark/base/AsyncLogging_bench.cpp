#include "chaoxi/base/AsyncLogging.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <latch>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

namespace
{

constexpr std::size_t kMessagesPerProducer = 25'000;
constexpr std::string_view kMessage =
    "This is a deterministic asynchronous log record used to measure durable "
    "end-to-end throughput. 0123456789\n";

[[nodiscard]] std::filesystem::path outputDirectory()
{
    if (const char* directory = std::getenv("CHAOXI_BENCHMARK_LOG_DIR"))
    {
        return directory;
    }
    return std::filesystem::temp_directory_path();
}

[[nodiscard]] std::uintmax_t fileBytesWithPrefix(
    const std::filesystem::path& directory, std::string_view prefix)
{
    std::uintmax_t bytes = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.path().filename().string().starts_with(prefix))
        {
            bytes += entry.file_size();
        }
    }
    return bytes;
}

void removeFilesWithPrefix(const std::filesystem::path& directory,
                           std::string_view prefix)
{
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.path().filename().string().starts_with(prefix))
        {
            std::filesystem::remove(entry.path());
        }
    }
}

void BM_AsyncLogging_EndToEnd(benchmark::State& state)
{
    const auto producerCount = static_cast<std::size_t>(state.range(0));
    const auto directory = outputDirectory();
    std::uint64_t totalMessages = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t totalDropped = 0;
    static std::atomic<std::uint64_t> sequence{};

    for (auto _ : state)
    {
        state.PauseTiming();
        const auto prefix =
            "async_logging_bench_" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        const auto basename = (directory / prefix).string();
        chaoxi::AsyncLogging log(basename, 1024ULL * 1024 * 1024);
        log.start();

        std::latch ready{static_cast<std::ptrdiff_t>(producerCount)};
        std::latch start{1};
        std::vector<std::jthread> producers;
        producers.reserve(producerCount);
        for (std::size_t producer = 0; producer < producerCount; ++producer)
        {
            producers.emplace_back(
                [&]
                {
                    ready.count_down();
                    start.wait();
                    for (std::size_t message = 0; message < kMessagesPerProducer;
                         ++message)
                    {
                        log.append(kMessage);
                    }
                });
        }
        ready.wait();

        state.ResumeTiming();
        const auto started = std::chrono::steady_clock::now();
        start.count_down();
        producers.clear();
        log.stop();
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started);
        state.SetIterationTime(elapsed.count());
        state.PauseTiming();

        const auto statistics = log.statistics();
        const auto fileBytes = fileBytesWithPrefix(directory, prefix);
        const auto expectedMessages = producerCount * kMessagesPerProducer;
        const auto expectedBytes = expectedMessages * kMessage.size();
        if (statistics.acceptedMessages != expectedMessages ||
            statistics.acceptedBytes != expectedBytes ||
            statistics.writtenMessages != expectedMessages ||
            statistics.writtenBytes != expectedBytes ||
            statistics.droppedMessages != 0 || fileBytes != expectedBytes)
        {
            state.SkipWithError("接收、写入和文件字节数不一致");
        }

        totalMessages += statistics.writtenMessages;
        totalBytes += statistics.writtenBytes;
        totalDropped += statistics.droppedMessages;
        removeFilesWithPrefix(directory, prefix);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(totalMessages));
    state.SetBytesProcessed(static_cast<std::int64_t>(totalBytes));
    state.counters["dropped_messages"] = static_cast<double>(totalDropped);
}

BENCHMARK(BM_AsyncLogging_EndToEnd)
    ->ArgName("producers")
    ->Arg(1)
    ->Arg(4)
    ->Arg(8)
    ->UseManualTime();

}  // namespace
