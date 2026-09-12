#include "chaoxi/base/LogFile.hpp"
#include "chaoxi/base/Logging.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

namespace
{

std::uint64_t g_totalBytes = 0;
int g_nullFd = -1;
std::unique_ptr<chaoxi::LogFile> g_logFile;

void nopOutput(std::string_view message)
{
    g_totalBytes += message.size();
}

void nullOutput(std::string_view message)
{
    g_totalBytes += message.size();
    if (g_nullFd >= 0)
    {
        (void)::write(g_nullFd, message.data(), message.size());
    }
}

void fileOutput(std::string_view message)
{
    g_totalBytes += message.size();
    g_logFile->append(message);
}

void dummyFlush() {}

bool noFinalize()
{
    return true;
}

bool flushFile()
{
    g_logFile->flush();
    return true;
}

bool syncFile()
{
    return g_logFile->sync();
}

using Finalize = bool (*)();

void bench(std::string_view type,
           chaoxi::Logger::OutputFunc output,
           Finalize finalize)
{
    chaoxi::Logger::setOutput(output);
    chaoxi::Logger::setFlush(dummyFlush);
    g_totalBytes = 0;
    constexpr int kBatchSize = 1'000'000;
    constexpr std::string_view message =
        "123456789012345678901234567890123456789012345678901234567890";

    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < kBatchSize; ++index)
    {
        LOG_INFO << message << ' ' << index;
    }
    if (!finalize())
    {
        throw std::runtime_error("基准日志文件持久化失败");
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - start;
    const auto messagesPerSecond = kBatchSize / elapsed.count();
    const auto mebibytesPerSecond =
        g_totalBytes / elapsed.count() / (1024.0 * 1024.0);

    std::printf("%-15.*s | %7.1f w | %7.1f MiB/s | 平均长度：%zu 字节\n",
                static_cast<int>(type.size()), type.data(),
                messagesPerSecond / 10'000.0, mebibytesPerSecond,
                g_totalBytes / kBatchSize);
}

[[nodiscard]] std::filesystem::path directoryFromEnvironment(
    const char* variable, const std::filesystem::path& fallback)
{
    if (const char* value = std::getenv(variable))
    {
        return value;
    }
    return fallback;
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

void benchFile(std::string_view label,
               const std::filesystem::path& directory,
               Finalize finalize)
{
    std::filesystem::create_directories(directory);
    const auto prefix = std::string(label) + "_log_bench";
    const auto basename = (directory / prefix).string();
    g_logFile = std::make_unique<chaoxi::LogFile>(basename,
                                                  1024ULL * 1024 * 1024, false);
    bench(label, fileOutput, finalize);
    g_logFile.reset();
    removeFilesWithPrefix(directory, prefix);
}

}  // namespace

int main()
{
    std::printf(
        "==============================================================\n");
    std::printf("目标场景        | 消息数/秒   | 带宽        | 消息信息\n");
    std::printf(
        "--------------------------------------------------------------\n");

    bench("nop", nopOutput, noFinalize);

    g_nullFd = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (g_nullFd >= 0)
    {
        bench("/dev/null", nullOutput, noFinalize);
        (void)::close(g_nullFd);
    }

    const auto temporary = std::filesystem::temp_directory_path();
    const auto tmpfs = directoryFromEnvironment(
        "CHAOXI_BENCHMARK_TMPFS_DIR",
        std::filesystem::exists("/dev/shm") ? "/dev/shm" : temporary);
    const auto pageCache = directoryFromEnvironment(
        "CHAOXI_BENCHMARK_PAGE_CACHE_DIR", std::filesystem::current_path());
    const auto fsync =
        directoryFromEnvironment("CHAOXI_BENCHMARK_FSYNC_DIR", pageCache);

    benchFile("tmpfs", tmpfs, flushFile);
    benchFile("page-cache", pageCache, flushFile);
    benchFile("fsync", fsync, syncFile);

    std::printf(
        "==============================================================\n");
}
