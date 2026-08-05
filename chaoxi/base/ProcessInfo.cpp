#include "chaoxi/base/ProcessInfo.hpp"

#include <array>

#ifdef _WIN32
#include <Lmcons.h>
#include <windows.h>
#else
#include <pwd.h>
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

std::string username()
{
#ifdef _WIN32
    std::array<char, UNLEN + 1> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    if (::GetUserNameA(buffer.data(), &size) != 0)
    {
        return buffer.data();
    }
#else
    struct passwd pwd;
    struct passwd* result = nullptr;

    std::array<char, 8192> buffer{};
    if (::getpwuid_r(::getuid(), &pwd, buffer.data(), buffer.size(), &result) ==
            0 &&
        result != nullptr)
    {
        return pwd.pw_name;
    }
#endif
    return "unknownuser";
}

std::string pidString()
{
    return std::to_string(pid());
}
}  // namespace chaoxi::process_info
