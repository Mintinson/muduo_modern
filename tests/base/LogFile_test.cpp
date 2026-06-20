#include "chaoxi/base/FileUtil.hpp"
#include "chaoxi/base/LogFile.hpp"
#include "chaoxi/base/Logging.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <print>
#include <thread>

std::unique_ptr<chaoxi::LogFile> g_logFile;

void OutputFunc(std::string_view msg)
{
    g_logFile->append(msg);
}

void flushFunc()
{
    g_logFile->flush();
}

int main(int argc, char** argv)
{
    std::filesystem::path file(argv[0]);
    g_logFile =
        std::make_unique<chaoxi::LogFile>(file.filename().string(), 200 * 1000);

    chaoxi::Logger::setOutput(OutputFunc);
    chaoxi::Logger::setFlush(flushFunc);

    std::string line =
        "1234567890 abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ ";

    for (int i = 0; i < 10000; ++i)
    {
        LOG_INFO << line << i;

        // usleep(1000);
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }
}