#include "chaoxi/base/LogStream.hpp"

namespace chaoxi
{
namespace detail
{
template <std::size_t Size>
const char* FixedBuffer<Size>::debugString() noexcept
{
    *cur_ = '\0';
    return data_.data();
}

}  // namespace detail

// 国际单位制 (Base 1000)
// 返回字符串长度至多 5 个字符 (如 "999", "9.99k", "10.0M", "999G")
[[nodiscard]] std::string formatSI(int64_t n)
{
    constexpr std::array<std::string_view, 6> si_units = {"k", "M", "G",
                                                          "T", "P", "E"};
    return detail::formatUnit<1000.0>(n, si_units);
}

// 二进制单位制 (Base 1024)
// 返回字符串长度至多 6 个字符 (如 "1023", "9.99Ki", "10.0Mi", "1023Gi")
[[nodiscard]] std::string formatIEC(int64_t n)
{
    constexpr std::array<std::string_view, 6> iec_units = {"Ki", "Mi", "Gi",
                                                           "Ti", "Pi", "Ei"};
    return detail::formatUnit<1024.0>(n, iec_units);
}
}  // namespace chaoxi