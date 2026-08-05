#pragma once
#include <string>

#ifdef _WIN32
using pid_t = int;
#else
#include <sys/types.h>
#endif

namespace chaoxi::process_info
{
[[nodiscard]] pid_t pid();
[[nodiscard]] std::string hostname();
[[nodiscard]] std::string pidString();
[[nodiscard]] std::string username();

}  // namespace chaoxi::process_info
