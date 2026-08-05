#include "chaoxi/base/ProcessInfo.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace chaoxi::process_info
{
pid_t pid()
{
#ifdef _WIN32
    return static_cast<pid_t>(::GetCurrentProcessId());
#else
    return ::getpid();
#endif
}

std::string hostname()
{
    char buf[256];
#ifdef _WIN32
    DWORD size = static_cast<DWORD>(sizeof buf);
    if (::GetComputerNameA(buf, &size) != 0)
#else
    if (::gethostname(buf, sizeof buf) == 0)
#endif
    {
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }

    return "unknownhost";
}
}  // namespace chaoxi::process_info
