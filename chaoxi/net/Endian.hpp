#pragma once

#include <bit>
#include <concepts>
#include <cstdint>

namespace chaoxi::net::sockets
{
template <std::unsigned_integral T>
constexpr T hostToNetwork(T value) noexcept
{
    if constexpr (std::endian::native == std::endian::big)
    {
        return value;
    }
    else
    {
        return std::byteswap(value);
    }
}

template <std::unsigned_integral T>
constexpr T networkToHost(T value) noexcept
{
    return hostToNetwork(value);
}

inline constexpr auto hton16 = [](uint16_t v) constexpr noexcept
{ return hostToNetwork(v); };
inline constexpr auto hton32 = [](uint32_t v) constexpr noexcept
{ return hostToNetwork(v); };
inline constexpr auto hton64 = [](uint64_t v) constexpr noexcept
{ return hostToNetwork(v); };
inline constexpr auto ntoh16 = hton16;
inline constexpr auto ntoh32 = hton32;
inline constexpr auto ntoh64 = hton64;

}  // namespace chaoxi::net::sockets
