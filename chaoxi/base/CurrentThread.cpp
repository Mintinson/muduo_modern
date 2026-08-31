#include "chaoxi/base/CurrentThread.hpp"

#include <chrono>
#include <stacktrace>
#include <thread>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/syscall.h>
    #include <unistd.h>
#endif

namespace chaoxi::detail
{
[[nodiscard]] int gettid()
{
#ifdef _WIN32
    return static_cast<int>(::GetCurrentThreadId());
#else
    return static_cast<pid_t>(
        ::syscall(SYS_gettid));  // NOLINT(cppcoreguidelines-pro-type-vararg)
#endif
}
};  // namespace chaoxi::detail

namespace chaoxi::CurrentThread
{
namespace
{
const int mainThreadId = detail::gettid();
}  // namespace

thread_local int t_cachedTid = 0;
thread_local char t_tidString[32];
thread_local int t_tidStringLength = 6;
thread_local const char* t_threadName = "unknown";

std::string stackTrace(unsigned short skip)
{
    auto trace = std::stacktrace::current(skip);

    if (trace.empty())
    {
        return "Stack trace not available\n";
    }

    return std::to_string(trace);
}

void cacheTid()
{
    if (t_cachedTid == 0)
    {
        t_cachedTid = detail::gettid();
        t_tidStringLength =
            snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid);
    }
}

bool isMainThread() noexcept
{
    return tid() == mainThreadId;
}

void sleepUsec(int64_t usec) noexcept
{
    // struct timespec ts = {0, 0};
    // ts.tv_sec = static_cast<time_t>(usec / Timestamp::kMicroSecondsPerSecond);
    // ts.tv_nsec =
    //     static_cast<long>(usec % Timestamp::kMicroSecondsPerSecond * 1000);
    // ::nanosleep(&ts, NULL);
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
}

}  // namespace chaoxi::CurrentThread
