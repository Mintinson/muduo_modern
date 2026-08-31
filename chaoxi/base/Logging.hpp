#pragma once

#include "chaoxi/base/LogStream.hpp"

#include <cstddef>
#include <functional>
#include <source_location>
#include <string_view>

namespace chaoxi
{

class TimeZone;

class Logger
{
public:
    enum class LogLevel : std::uint8_t
    {
        TRACE,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL,
        NUM_LOG_LEVELS,
    };

    constexpr static std::string_view get_basename(
        std::string_view path) noexcept
    {
        auto pos = path.find_last_of("/\\");
        return pos == std::string_view::npos ? path : path.substr(pos + 1);
        // const char* slash =
        //     __builtin_strrchr(path.data(), '/');  // 编译器 intrinsic，极快
        // if (slash)
        // {
        //     return {slash + 1};
        // }
        // return std::string_view(path);
    }

    // 定义一个编译期生成的源码位置信息结构
    struct LogLocation
    {
        std::string_view basename;
        std::string_view function_name;
        uint_least32_t line;

        // consteval 强制在调用端（宏展开的地方）发生编译期求值
        consteval LogLocation(const std::source_location& loc =
                                  std::source_location::current()) noexcept
            : basename(get_basename(loc.file_name()))
            , function_name(loc.function_name())
            , line(loc.line())
        {
        }
    };

    // explicit Logger(
    //     LogLevel level,
    //     int savedErrno = 0,
    //     const std::source_location sl = std::source_location::current());
    explicit Logger(LogLevel level,
                    int savedErrno = 0,
                    LogLocation loc = LogLocation{});

    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    LogStream& stream() { return impl_.stream_; }

    static LogLevel logLevel() noexcept { return g_logLevel; }

    static void setLogLevel(LogLevel level);

    using OutputFunc = std::function<void(std::string_view)>;
    using FlushFunc = std::function<void()>;
    static void setOutput(OutputFunc);
    static void setFlush(FlushFunc);
    static void setTimeZone();

private:
    class Impl
    {
    public:
        static constexpr std::size_t kMicroSecondsWidth =
            8;                                         // 微秒宽度，固定为 6 位
        static constexpr std::size_t kTimeWidth = 25;  // 时间的总长度
        static_assert(kTimeWidth >= kMicroSecondsWidth);

        using LogLevel = Logger::LogLevel;

        // Impl(LogLevel level,
        //      int savedErrno,
        //      const std::source_location sl = std::source_location::current());
        constexpr Impl(LogLevel level,
                       int savedErrno,
                       LogLocation sl = LogLocation{});
        void formatTime();
        void finish();

        LogStream stream_;
        LogLevel level_;
        // std::source_location loc_;
        LogLocation loc_;
    };

    Impl impl_;
    static inline LogLevel g_logLevel = LogLevel::INFO;
};

}  // namespace chaoxi

#define LOG_TRACE                                                      \
    if (chaoxi::Logger::logLevel() <= chaoxi::Logger::LogLevel::TRACE) \
    chaoxi::Logger(chaoxi::Logger::LogLevel::TRACE).stream()
#define LOG_DEBUG                                                      \
    if (chaoxi::Logger::logLevel() <= chaoxi::Logger::LogLevel::DEBUG) \
    chaoxi::Logger(chaoxi::Logger::LogLevel::DEBUG).stream()
#define LOG_INFO                                                      \
    if (chaoxi::Logger::logLevel() <= chaoxi::Logger::LogLevel::INFO) \
    chaoxi::Logger(chaoxi::Logger::LogLevel::INFO).stream()
#define LOG_WARN  chaoxi::Logger(chaoxi::Logger::LogLevel::WARN).stream()
#define LOG_ERROR chaoxi::Logger(chaoxi::Logger::LogLevel::ERROR).stream()
#define LOG_FATAL chaoxi::Logger(chaoxi::Logger::LogLevel::FATAL).stream()
#define LOG_SYSERR \
    chaoxi::Logger(chaoxi::Logger::LogLevel::ERROR, errno).stream()
#define LOG_SYSFATAL \
    chaoxi::Logger(chaoxi::Logger::LogLevel::FATAL, errno).stream()

///
/// FLOG_XX: 格式化日志宏 —— 直接 format_to_n 到栈缓冲区，零堆分配
///
/// 与 LOG_XX 的对比:
///   LOG_INFO << val        → operator<< + to_chars 写 FixedBuffer（快）
///   FLOG_INFO("{}", val)   → format_to_n 写 FixedBuffer（同样快！）
///
/// 优化原理:
///   旧: LOG_INFO << std::format(f, args...)
///       └→ std::format 先创建临时 std::string（堆分配），再 append
///   新: Logger(INFO).stream().format(f, args...)
///       └→ std::format_to_n 直接写入栈上 FixedBuffer（4KB），零分配
///
/// 注意: FLOG_XX 至少需要一个参数（format 字符串），后续参数可选。
///       FLOG_INFO("plain")   → 等同于 LOG_INFO << "plain"
///       FLOG_INFO("{}", val) → 格式化单个值
///
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define FLOG_TRACE(...)                                                \
    if (chaoxi::Logger::logLevel() <= chaoxi::Logger::LogLevel::TRACE) \
    chaoxi::Logger(chaoxi::Logger::LogLevel::TRACE).stream().format(__VA_ARGS__)
#define FLOG_DEBUG(...)                                                \
    if (chaoxi::Logger::logLevel() <= chaoxi::Logger::LogLevel::DEBUG) \
    chaoxi::Logger(chaoxi::Logger::LogLevel::DEBUG).stream().format(__VA_ARGS__)
#define FLOG_INFO(...)                                                \
    if (chaoxi::Logger::logLevel() <= chaoxi::Logger::LogLevel::INFO) \
    chaoxi::Logger(chaoxi::Logger::LogLevel::INFO).stream().format(__VA_ARGS__)
// WARN/ERROR/FATAL 没 if 过滤，无条件执行
#define FLOG_WARN(...) \
    chaoxi::Logger(chaoxi::Logger::LogLevel::WARN).stream().format(__VA_ARGS__)
#define FLOG_ERROR(...) \
    chaoxi::Logger(chaoxi::Logger::LogLevel::ERROR).stream().format(__VA_ARGS__)
#define FLOG_FATAL(...) \
    chaoxi::Logger(chaoxi::Logger::LogLevel::FATAL).stream().format(__VA_ARGS__)
#define FLOG_SYSERR(...)                                   \
    chaoxi::Logger(chaoxi::Logger::LogLevel::ERROR, errno) \
        .stream()                                          \
        .format(__VA_ARGS__)
#define FLOG_SYSFATAL(...)                                 \
    chaoxi::Logger(chaoxi::Logger::LogLevel::FATAL, errno) \
        .stream()                                          \
        .format(__VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)
