#include "chaoxi/base/ProcessInfo.hpp"

#include <unistd.h>

namespace chaoxi::process_info
{
pid_t pid()
{
    return ::getpid();
}

std::string hostname()
{
    char buf[256];
    if (::gethostname(buf, sizeof buf) == 0)
    {
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }

    return "unknownhost";
}
}  // namespace chaoxi::process_info