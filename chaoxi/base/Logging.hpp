#pragma once

#include "chaoxi/base/LogStream.hpp"

#include <functional>
#include <source_location>

namespace chaoxi
{

class TimeZone;

class Logger
{
public:
    enum LogLevel : std::uint8_t
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
    }

    explicit Logger(std::source_location sl = std::source_location::current());
    // explicit Logger(LogLevel level,
    //                 std::source_location sl =
    //                 std::source_location::current());
    explicit Logger(
        LogLevel level,
        int savedErrno = 0,
        const std::source_location sl = std::source_location::current());

    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    LogStream& stream() { return impl_.stream_; }

    static LogLevel logLevel() noexcept { return g_logLevel; }

    static void setLogLevel(LogLevel level);

    using OutputFunc = std::function<void(std::span<const char>)>;
    using FlushFunc = std::function<void()>;
    static void setOutput(OutputFunc);
    static void setFlush(FlushFunc);
    static void setTimeZone();

private:
    class Impl
    {
    public:
        using LogLevel = Logger::LogLevel;

        Impl(LogLevel level,
             int savedErrno,
             const std::source_location sl = std::source_location::current());
        void formatTime();
        void finish();

        LogStream stream_;
        LogLevel level_;
        std::source_location loc_;
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

#define FLOG_TRACE(f, ...)    LOG_TRACE << std::format(f, __VA_ARGS__)
#define FLOG_WARN(f, ...)     LOG_WARN << std::format(f, __VA_ARGS__)
#define FLOG_ERROR(f, ...)    LOG_ERROR << std::format(f, __VA_ARGS__)
#define FLOG_DEBUG(f, ...)    LOG_DEBUG << std::format(f, __VA_ARGS__)
#define FLOG_INFO(f, ...)     LOG_INFO << std::format(f, __VA_ARGS__)
#define FLOG_SYSERR(f, ...)   LOG_SYSERR << std::format(f, __VA_ARGS__)
#define FLOG_SYSFATAL(f, ...) LOG_SYSFATAL << std::format(f, __VA_ARGS__)
