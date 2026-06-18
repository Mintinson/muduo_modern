#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/CurrentThread.hpp"

#include <chrono>

namespace chaoxi {
namespace {
void defaultOutput(std::span<const char> msg) {
    std::fwrite(msg.data(), 1, msg.size(), stdout);
}

void defaultFlush() {
    std::fflush(stdout);
}

Logger::OutputFunc g_output = defaultOutput;
Logger::FlushFunc g_flush = defaultFlush;

constexpr std::array<std::string_view,
                     static_cast<size_t>(Logger::LogLevel::NUM_LOG_LEVELS)>
    LogLevelName = {"TRACE ", "DEBUG ", "INFO  ", "WARN  ", "ERROR ", "FATAL "};

thread_local char t_time[32];
thread_local std::chrono::seconds t_lastSecond{};

}  // namespace

void Logger::setOutput(OutputFunc out) {
    g_output = std::move(out);
}

void Logger::setFlush(FlushFunc flush) {
    g_flush = std::move(flush);
}

Logger::Logger(LogLevel level, int savedErrno, const std::source_location loc)
    : impl_(level, savedErrno, loc) {}

Logger::~Logger() {
    impl_.finish();
    const auto& buf = stream().buffer();
    g_output(buf.span());  // 使用上一次重构的 span() 接口

    if (impl_.level_ == LogLevel::FATAL) {
        g_flush();
        std::abort();
    }
}

void Logger::Impl::finish() {
    // 4. 调用编译期的 get_basename 和 source_location 提取文件、行号和函数名
    stream_ << " - " << loc_.function_name() << " " << get_basename(loc_.file_name())
            << ':' << loc_.line() << '\n';
}

Logger::Impl::Impl(LogLevel level, int savedErrno, const std::source_location sl)
    : level_(level)
    , loc_(sl) {
    formatTime();
    stream_ << CurrentThread::tidString() << " ";
    stream_ << LogLevelName[static_cast<size_t>(level)];

    if (savedErrno != 0) {
        std::error_code ec(savedErrno, std::system_category());
        stream_ << ec.message() << " (errno=" << savedErrno << ") ";
    }
}

void Logger::Impl::formatTime() {
    using namespace std::chrono;
    auto now = std::chrono::system_clock::now();
    auto sys_secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - sys_secs).count();

    // 保留原版的按秒缓存机制，避免每条日志都完整格式化日期时间
    if (sys_secs.time_since_epoch() != t_lastSecond) {
        t_lastSecond = sys_secs.time_since_epoch();

        // 3. 搭配 C++20 chrono formatting 和 std::format_to 写入缓存
        // 注意：C++20 std::chrono::zoned_time 可以完美处理时区，这里简化为本地时间
        std::format_to_n(t_time, sizeof(t_time), "{:%Y%m%d %H:%M:%S}",
                         std::chrono::current_zone()->to_local(now));
    }

    // 将缓存的秒级字符串和当前的微秒拼接写入 Stream
    stream_ << std::string_view{t_time, 17} << std::format(".{:06d} ", us);
}

}  // namespace chaoxi