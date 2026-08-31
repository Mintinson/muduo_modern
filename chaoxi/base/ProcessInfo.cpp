#include "chaoxi/base/ProcessInfo.hpp"

#include <array>

// clang-format off
#ifdef _WIN32
    #include <windows.h>
    #include <Lmcons.h>
#else
    #include <pwd.h>
    #include <unistd.h>
#endif
// clang-format on

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
    std::array<char, 256> buf{};  // NOLINT(readability-magic-numbers)
#ifdef _WIN32
    DWORD size = static_cast<DWORD>(buf.size());
    if (::GetComputerNameA(buf.data(), &size) != 0)
#else
    if (::gethostname(buf.data(), buf.size()) == 0)
#endif
    {
        buf.back() = '\0';
        return {buf.begin(), buf.end()};
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
    struct passwd pwd{};
    struct passwd* result = nullptr;

    std::array<char, 8192> buffer{};  // NOLINT(readability-magic-numbers)
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
