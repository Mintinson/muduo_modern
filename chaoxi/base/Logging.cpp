/**
 * @file Logging.cpp
 * @author your name (you@domain.com)
 * @brief

 * 优化 1： formatTime 中对于毫秒部分的格式化
 *  a：将原来的 << std::format() 改成 stream.format(), 直接在 stream
 * 的缓冲中构造。避免中间临时字符串的生成.(<= 5% 的性能提升)
 *  b：由于确定字符串的大小，可以直接在 FixedBuffer 尾部写入微秒数，避免 format
 * 的运行时开销 (50% 左右的性能提升)
 * 优化 2： std::source_location 的 file_name() 返回的是const char*,
 长度信息丢失。导致 get_basename
 无法在编译期获取basename，且需要额外的strlen开销。
 * 解决方法：使用 LogLocation 包装 std::source_location，利用 consteval
 强制在编译期获取 basename，避免运行时 strlen 开销。（约10%左右的性能提升）
 * @version 0.1
 * @date 2026-06-22
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "chaoxi/base/Logging.hpp"

#include "chaoxi/base/CurrentThread.hpp"

#include <array>
#include <chrono>
#include <cstddef>

namespace chaoxi
{
namespace
{
void defaultOutput(std::string_view msg)
{
    std::fwrite(msg.data(), 1, msg.size(), stdout);
}

void defaultFlush()
{
    std::fflush(stdout);
}

Logger::OutputFunc g_output = defaultOutput;
Logger::FlushFunc g_flush = defaultFlush;

constexpr std::array<std::string_view,
                     static_cast<size_t>(Logger::LogLevel::NUM_LOG_LEVELS)>
    LogLevelName = {"TRACE ", "DEBUG ", "INFO  ", "WARN  ", "ERROR ", "FATAL "};

thread_local std::array<char, 32> t_time{};
// thread_local char t_time[32];
thread_local std::chrono::seconds t_lastSecond{};

}  // namespace

void Logger::setOutput(OutputFunc out)
{
    g_output = std::move(out);
}

void Logger::setFlush(FlushFunc flush)
{
    g_flush = std::move(flush);
}

// Logger::Logger(LogLevel level, int savedErrno, const std::source_location loc)
//     : impl_(level, savedErrno, loc)
// {
// }

Logger::Logger(LogLevel level, int savedErrno, LogLocation loc)
    : impl_(level, savedErrno, loc)
{
}

Logger::~Logger()
{
    impl_.finish();
    const auto& buf = stream().buffer();
    g_output(buf.view());  // 使用上一次重构的 span() 接口

    if (impl_.level_ == LogLevel::FATAL)
    {
        g_flush();
        std::abort();
    }
}

void Logger::Impl::finish()
{
    // 4. 调用编译期的 get_basename 和 source_location 提取文件、行号和函数名
    // stream_ << " - " << loc_.function_name() << " "
    //         << get_basename(loc_.file_name()) << ':' << loc_.line() << '\n';
    // stream_ << " - " << get_basename(loc_.file_name()) << ':' << loc_.line()
    //         << '\n';
    stream_ << " - " << " " << loc_.basename << ':' << loc_.line << '\n';
}

//  Logger::Impl::Impl(LogLevel level, int savedErrno, const std::source_location
//  sl)
constexpr Logger::Impl::Impl(LogLevel level, int savedErrno, LogLocation sl)
    : level_(level)
    , loc_(sl)
{
    formatTime();
    stream_ << CurrentThread::tidString() << ' ';
    stream_ << LogLevelName[static_cast<size_t>(level)];

    if (savedErrno != 0)
    {
        std::error_code ec(savedErrno, std::system_category());
        stream_ << ec.message() << " (errno=" << savedErrno << ") ";
    }
}

void Logger::Impl::formatTime()
{
    using namespace std::chrono;
    auto now = std::chrono::system_clock::now();
    auto sys_secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - sys_secs)
            .count();

    // 保留原版的按秒缓存机制，避免每条日志都完整格式化日期时间

    if (sys_secs.time_since_epoch() != t_lastSecond)
    {
        t_lastSecond = sys_secs.time_since_epoch();

        // 3. 搭配 C++20 chrono formatting 和 std::format_to 写入缓存
        // 注意：C++20 std::chrono::zoned_time
        // 可以完美处理时区，这里简化为本地时间
        std::format_to_n(t_time.data(), t_time.size(), "{:%Y%m%d %H:%M:%S}",
                         std::chrono::current_zone()->to_local(now));
    }

    // 将缓存的秒级字符串和当前的微秒拼接写入 Stream
    stream_ << std::string_view{t_time.data(), kTimeWidth - kMicroSecondsWidth};
    // << std::format(".{:06d} ", us);
    // 零分配、零拷贝，直接写到 FixedBuffer 尾部
    // stream_.format(".{:06d} ", us);
    // 更加高效的方式：直接在 FixedBuffer 尾部写入微秒数，避免 format
    // 的运行时开销

    if (stream_.buffer().avail() >= kMicroSecondsWidth)
    {
        auto buf = stream_.buffer().writeSpan(kMicroSecondsWidth);
        // char* buf = stream_.buffer().current();
        buf[0] = '.';
        buf[kMicroSecondsWidth - 1] = ' ';

        // 逆序填充数字
        for (std::size_t j = kMicroSecondsWidth - 2; j >= 1; --j)
        {
            buf[j] = static_cast<char>('0' + (us % 10));
            us /= 10;
        }
        // stream_.buffer().add(8);
    }
}

}  // namespace chaoxi