///
/// @file LogFile.cpp
/// @brief LogFile 实现 —— 自动滚动 + 延迟 flush 的日志文件写入器
///

#include "chaoxi/base/LogFile.hpp"

#include "chaoxi/base/FileUtil.hpp"
#include "chaoxi/base/ProcessInfo.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <utility>

namespace chaoxi
{

LogFile::LogFile(std::string basename,
                 std::size_t rollSize,
                 bool threadSafe,
                 int flushInterval,
                 int checkEveryN)
    : basename_(std::move(basename))
    , rollSize_(rollSize)
    , flushInterval_(flushInterval)
    , checkEveryN_(checkEveryN)
    // threadSafe=true 才创建 mutex，否则为 nullptr（零开销，不用锁）
    , mutex_(threadSafe ? std::make_unique<std::mutex>() : nullptr)
{
    // basename 不应含路径分隔符（文件名只用于日志命名，不含目录）
    assert(basename.contains('/'));
    rollFile();  // 构造时立即创建第一个日志文件
}

LogFile::~LogFile() = default;

///
/// 线程安全写入：如果构造时指定了 threadSafe，加锁后调用 append_unlocked
///
void LogFile::append(std::string_view logline)
{
    if (mutex_)
    {
        std::scoped_lock lock(*mutex_);
        append_unlocked(logline);
    }
    else
    {
        append_unlocked(logline);
    }
}

void LogFile::flush()
{
    if (mutex_)
    {
        std::scoped_lock lock(*mutex_);
        file_->flush();
    }
    else
    {
        file_->flush();
    }
}

///
/// 核心写入逻辑：写入数据 → 检查是否需要滚动 → 检查是否需要 flush
///
/// 滚动触发条件（满足任一即触发）：
///   1. 文件已写字节数 > rollSize_（大小滚动）
///   2. 日志跨天了（日期滚动）—— 每 checkEveryN_ 条日志才检查一次以降低开销
///
/// flush 触发条件：
///   - 每 checkEveryN_ 条日志时检查，距上次 flush 超过 flushInterval_ 秒则 flush
///   - 注意：发生滚动时自动 flush（新文件自然为空），不需额外检查
///
void LogFile::append_unlocked(std::string_view logline)
{
    file_->append(logline);

    if (file_->writtenBytes() > rollSize_)
    {
        // 大小超限 → 立即滚动
        rollFile();
    }
    else
    {
        ++count_;
        if (count_ >= checkEveryN_)
        {
            // 每 N 条日志检查一次日期和 flush
            count_ = 0;
            auto now = std::chrono::system_clock::now();
            auto thisPeriod = std::chrono::floor<std::chrono::days>(now);

            if (thisPeriod != startOfPeriod_)
            {
                // 跨天了 → 滚动到新文件
                rollFile();
            }
            else if (now - lastFlush_ > std::chrono::seconds(flushInterval_))
            {
                // 还没跨天，但距上次 flush 太久 → flush
                lastFlush_ = now;
                file_->flush();
            }
        }
    }
}

///
/// 创建新的日志文件。文件名格式：basename.YYYYMMDD-HHMMSS.hostname.pid.log
///
bool LogFile::rollFile()
{
    std::chrono::system_clock::time_point now;
    std::string filename = getLogFileName(basename_, now);

    // 按天对齐的时间点，用作跨天检测的标尺
    auto start = std::chrono::floor<std::chrono::days>(now);

    if (now > lastRoll_)
    {
        lastRoll_ = now;
        lastFlush_ = now;
        startOfPeriod_ = start;
        file_ = std::make_unique<file_util::AppendFile>(filename);
        return true;
    }
    return false;  // 同一秒内已滚动过，跳过
}

std::string LogFile::getLogFileName(const std::string& basename,
                                    std::chrono::system_clock::time_point& now)
{
    now = std::chrono::system_clock::now();
    // C++20 std::chrono::zoned_time 自动处理本地时区
    auto truncated = std::chrono::time_point_cast<std::chrono::seconds>(now);
    std::chrono::zoned_time local_time{std::chrono::current_zone(), truncated};
    // 格式: basename.YYYYMMDD-HHMMSS.hostname.pid.log
    return std::format("{}.{:%Y%m%d-%H%M%S}.{}.{}.log", basename, local_time,
                       process_info::hostname(), process_info::pid());
}

}  // namespace chaoxi
