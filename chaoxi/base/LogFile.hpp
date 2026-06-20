#pragma once

///
/// @file LogFile.hpp
/// @brief 日志文件滚动写入 —— 支持按大小或按天自动切分
///
/// 设计要点：
///   - 按大小滚动：当 writtenBytes > rollSize_ 时切新文件
///   - 按天滚动：每 checkEveryN 条日志检查是否跨天，跨天则切新文件
///   - 线程安全：可选 std::mutex 保护（构造参数 threadSafe）
///   - 延迟 flush：每 checkEveryN 条日志检查是否需要 flush（按 flushInterval_ 秒间隔）
///   - 文件命名：basename.YYYYMMDD-HHMMSS.hostname.pid.log
///

#include <chrono>
#include <mutex>
#include <string>

namespace chaoxi {
namespace file_util {
class AppendFile;
}  // namespace file_util

class LogFile {
public:
    /// @param basename      日志文件基本名（不含路径，不含扩展名）
    /// @param rollSize      文件大小滚动阈值（字节）
    /// @param threadSafe    是否加锁（默认 true）
    /// @param flushInterval flush 间隔秒数（默认 3）
    /// @param checkEveryN   每 N 条日志检查一次滚动/flush（默认 1024）
    LogFile(std::string basename,
            off_t rollSize,
            bool threadSafe = true,
            int flushInterval = 3,
            int checkEveryN = 1024);
    ~LogFile();
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;
    LogFile(LogFile&&) = delete;
    LogFile& operator=(LogFile&&) = delete;

    /// 追加一条日志（线程安全视构造参数而定）
    void append(std::string_view logline);

    /// 强制 flush 到磁盘
    void flush();

    /// 手动触发文件滚动（创建新日志文件）
    bool rollFile();

private:
    void append_unlocked(std::string_view logline);

    /// 生成文件名: basename.YYYYMMDD-HHMMSS.hostname.pid.log
    static std::string getLogFileName(const std::string& basename,
                                      std::chrono::system_clock::time_point& now);

    const std::string basename_;
    const off_t rollSize_;
    const int flushInterval_;
    const int checkEveryN_;

    int count_{0};                           ///< 日志条数计数器，用于降低检查频率
    std::unique_ptr<std::mutex> mutex_;      ///< nullptr = 不线程安全

    std::chrono::system_clock::time_point startOfPeriod_{};  ///< 当前日志文件所属的日期起点
    std::chrono::system_clock::time_point lastRoll_{};       ///< 上次滚动时间
    std::chrono::system_clock::time_point lastFlush_{};      ///< 上次 flush 时间

    std::unique_ptr<file_util::AppendFile> file_;   ///< 当前日志文件的写入句柄
};
}  // namespace chaoxi
