#include "chaoxi/base/CurrentThread.hpp"
#include "chaoxi/base/FileUtil.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/base/ThreadLocalSingleton.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/base/Utility.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <gtest/gtest.h>
#include <unistd.h>

namespace
{

using namespace std::chrono_literals;

struct ThreadState
{
    int value = 0;
};

class FileUtilTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        path_ = std::filesystem::temp_directory_path() /
                ("chaoxi_file_util_" + std::to_string(::getpid()) + "_" +
                 std::to_string(sequence_++));
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    inline static unsigned sequence_ = 0;
    std::filesystem::path path_;
};

TEST(TimestampTest, AddsIntegralAndFractionalSeconds)
{
    const chaoxi::Timestamp epoch{};

    EXPECT_EQ(chaoxi::addTime(epoch, 2), epoch + std::chrono::seconds(2));
    EXPECT_EQ(chaoxi::addTime(epoch, 0.25),
              epoch + std::chrono::milliseconds(250));
    EXPECT_EQ(chaoxi::addTime(epoch, -1.5),
              epoch - std::chrono::milliseconds(1500));
}

TEST(ThreadLocalSingletonTest, ReturnsSameObjectWithinOneThread)
{
    using Singleton = chaoxi::ThreadLocalSingleton<ThreadState>;

    Singleton::instance().value = 42;

    EXPECT_EQ(Singleton::pointer(), &Singleton::instance());
    EXPECT_EQ(Singleton::instance().value, 42);
}

TEST(ThreadLocalSingletonTest, KeepsIndependentStatePerThread)
{
    using Singleton = chaoxi::ThreadLocalSingleton<ThreadState>;
    Singleton::instance().value = 7;
    int workerInitial = -1;
    int workerFinal = -1;
    ThreadState* workerAddress = nullptr;

    std::jthread worker(
        [&]
        {
            workerInitial = Singleton::instance().value;
            Singleton::instance().value = 99;
            workerFinal = Singleton::instance().value;
            workerAddress = Singleton::pointer();
        });
    worker.join();

    EXPECT_EQ(workerInitial, 0);
    EXPECT_EQ(workerFinal, 99);
    EXPECT_NE(workerAddress, Singleton::pointer());
    EXPECT_EQ(Singleton::instance().value, 7);
}

TEST(CurrentThreadTest, CachesStableThreadIdentity)
{
    const int tid = chaoxi::CurrentThread::tid();
    const std::string tidText(chaoxi::CurrentThread::tidString());

    EXPECT_GT(tid, 0);
    EXPECT_EQ(chaoxi::CurrentThread::tid(), tid);
    EXPECT_NE(tidText.find(std::to_string(tid)), std::string::npos);
    EXPECT_TRUE(chaoxi::CurrentThread::isMainThread());
}

TEST(CurrentThreadTest, WorkerHasDifferentIdentity)
{
    const int mainTid = chaoxi::CurrentThread::tid();
    int workerTid = 0;
    bool workerIsMain = true;

    std::jthread worker(
        [&]
        {
            workerTid = chaoxi::CurrentThread::tid();
            workerIsMain = chaoxi::CurrentThread::isMainThread();
        });
    worker.join();

    EXPECT_GT(workerTid, 0);
    EXPECT_NE(workerTid, mainTid);
    EXPECT_FALSE(workerIsMain);
}

TEST(ProcessInfoTest, MatchesOperatingSystem)
{
    EXPECT_EQ(chaoxi::process_info::pid(), ::getpid());
    EXPECT_FALSE(chaoxi::process_info::hostname().empty());
}

TEST(StringHashTest, SupportsHeterogeneousLookup)
{
    using Map = std::unordered_map<std::string, int, chaoxi::base::StringHash,
                                   std::equal_to<>>;
    Map values{
        {"answer", 42}
    };

    const auto it = values.find(std::string_view{"answer"});

    ASSERT_NE(it, values.end());
    EXPECT_EQ(it->second, 42);
}

TEST_F(FileUtilTest, ReadsRegularFileAndHonorsMaximumSize)
{
    {
        std::ofstream output(path_, std::ios::binary);
        output << "0123456789";
    }

    const auto result = chaoxi::file_util::readSmallFile(path_, 4);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->content, "0123");
    EXPECT_EQ(result->meta.fileSize, 10U);
}

TEST_F(FileUtilTest, ReportsMissingFileAndDirectoryErrors)
{
    const auto missing = chaoxi::file_util::readSmallFile(path_);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().value(), ENOENT);

    ASSERT_TRUE(std::filesystem::create_directory(path_));
    const auto directory = chaoxi::file_util::readSmallFile(path_);
    ASSERT_FALSE(directory.has_value());
    EXPECT_EQ(directory.error().value(), EISDIR);
}

TEST_F(FileUtilTest, AppendFilePersistsBinarySafeContent)
{
    const std::string first{"abc\0def", 7};
    {
        chaoxi::file_util::AppendFile file(path_.string());
        file.append(first);
        file.append("tail");
        EXPECT_EQ(file.writtenBytes(), 11U);
        file.flush();
    }

    const auto result = chaoxi::file_util::readSmallFile(path_);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->content, first + "tail");
}

}  // namespace
