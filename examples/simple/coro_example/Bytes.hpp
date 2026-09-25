#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace coro_example
{

/// 把字符串视图转换成只读字节视图。
///
/// AsyncFd / AsyncSocket 的写接口使用 std::span<const std::byte>，
/// 这里统一做一次转换，避免每个示例都写 reinterpret_cast + span。
[[nodiscard]] inline std::span<const std::byte> asBytes(
    std::string_view value) noexcept
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

}  // namespace coro_example
