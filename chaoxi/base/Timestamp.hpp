#pragma once
#include <chrono>

namespace chaoxi
{

using Timestamp = std::chrono::system_clock::time_point;

///
/// Add @c seconds to given timestamp.
///
/// @return timestamp+seconds as Timestamp
///
template <typename T>
[[nodiscard]] Timestamp addTime(Timestamp timestamp, T seconds) noexcept
{
    auto duration = std::chrono::duration<T>(seconds);
    return timestamp +
           std::chrono::duration_cast<std::chrono::system_clock::duration>(
               duration);
}

}  // namespace chaoxi