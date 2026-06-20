#pragma once
#include <string>

#include <sys/types.h>

namespace chaoxi::process_info
{
[[nodiscard]] pid_t pid();
[[nodiscard]] std::string hostname();
}  // namespace chaoxi::process_info