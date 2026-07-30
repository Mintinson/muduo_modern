#include "chaoxi/base/CurrentThread.hpp"

#include <chrono>
#include <stacktrace>
#include <thread>

#include <sys/syscall.h>
#include <unistd.h>

namespace chaoxi::detail
{
[[nodiscard]] pid_t gettid()
{
    return static_cast<pid_t>(::syscall(SYS_gettid));
}
};  // namespace chaoxi::detail

namespace chaoxi::CurrentThread
{

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

void sleepUsec(int64_t usec) noexcept
{
    // struct timespec ts = {0, 0};
    // ts.tv_sec = static_cast<time_t>(usec / Timestamp::kMicroSecondsPerSecond);
    // ts.tv_nsec =
    //     static_cast<long>(usec % Timestamp::kMicroSecondsPerSecond * 1000);
    // ::nanosleep(&ts, NULL);
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
}

bool isMainThread() noexcept
{
    return tid() == static_cast<int>(::getpid());
}

}  // namespace chaoxi::CurrentThread
