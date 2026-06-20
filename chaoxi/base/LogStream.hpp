#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace chaoxi
{

namespace detail
{
constexpr int kSmallBuffer = 4000;
constexpr int kLargeBuffer = 4000 * 1000;

template <int Size>
class FixedBuffer
{
public:
    FixedBuffer() noexcept : cur_(data_.data()) { setCookie(cookieStart); }

    ~FixedBuffer() { setCookie(cookieEnd); }

    FixedBuffer(const FixedBuffer&) = delete;
    FixedBuffer& operator=(const FixedBuffer&) = delete;

    void append(std::span<const char> buf) noexcept
    {
        // FIXME: append partially
        if (avail() > buf.size())
        {
            std::ranges::copy(buf, cur_);
            cur_ += buf.size();
        }
    }

    void append(const char* buf, std::size_t len) noexcept
    {
        append(std::span<const char>{buf, len});
    }

    [[nodiscard]] const char* data() const noexcept { return data_.data(); }

    [[nodiscard]] std::size_t length() const noexcept
    {
        return static_cast<std::size_t>(cur_ - data_.data());
    }

    [[nodiscard]] char* current() noexcept { return cur_; }

    [[nodiscard]] std::size_t avail() const noexcept { return Size - length(); }

    void add(size_t len) noexcept { cur_ += len; }

    void reset() noexcept { cur_ = data_.data(); }

    void bzero() noexcept { std::ranges::fill(data_, '\0'); }

    using CookieFunc = void (*)();

    void setCookie(void (*cookie)()) noexcept { cookie_ = cookie; }

    [[nodiscard]] std::string toString() const
    {
        return std::string(data(), length());
    }

    [[nodiscard]] std::string_view toStringPiece() const noexcept
    {
        return std::string_view(data(), length());
    }

    // return current valid data
    [[nodiscard]] std::span<const char> span() const noexcept
    {
        return {data_.data(), length()};
    }

    [[nodiscard]] std::string_view view() const noexcept
    {
        return {data_.data(), length()};
    }

    // for used by GDB
    const char* debugString() noexcept;

private:
    //   const char *end() const { return data_ + sizeof(data_); }
    // Must be outline function for cookies.
    static void cookieStart() noexcept {}

    static void cookieEnd() noexcept {}

    CookieFunc cookie_;
    std::array<char, Size> data_{};
    char* cur_ = data_.data();
};

template <double Base, std::size_t N>
[[nodiscard]] std::string formatUnit(
    int64_t n, const std::array<std::string_view, N>& units)
{
    if (n < 0)
    {
        return "-" + formatUnit<Base>(-n, units);
    }

    if (n < static_cast<int64_t>(Base))
    {
        return std::format("{}", n);
    }
    auto val = static_cast<double>(n);

    for (std::string_view unit : units)
    {
        val /= Base;
        // 动态精度控制：利用四舍五入的边界，保证 SI 最长 5 字符，IEC 最长 6 字符
        // 例如：9.994 -> 9.99 (4位), 9.996 -> 10.0 (4位)
        if (val < 9.995)
        {
            return std::format("{:.2f}{}", val, unit);
        }
        if (val < 99.95)
        {
            return std::format("{:.1f}{}", val, unit);
        }
        if (val < Base - 0.5)
        {
            return std::format("{:.0f}{}", val, unit);
        }
    }

    // Fallback: 兜底逻辑，防止超出预设单位范围（例如极大的数值）
    return std::format("{:.0f}{}", val, units.back());
}

}  // namespace detail

class LogStream
{
public:
    using Buffer = detail::FixedBuffer<detail::kSmallBuffer>;

    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;

    LogStream() = default;

    LogStream& operator<<(bool v) noexcept
    {
        using std::literals::operator""sv;
        buffer_.append(v ? "1"sv : "0"sv);
        return *this;
    };

    template <std::integral T>
        requires(!std::same_as<T, bool>)
    LogStream& operator<<(T v) noexcept
    {
        char buf[32];

        if (auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
            ec == std::errc{})
        {
            buffer_.append(std::span<const char>{buf, ptr});
        }
        return *this;
    }

    template <std::floating_point T>
    LogStream& operator<<(T v) noexcept
    {
        if (buffer_.avail() >= kMaxNumericSize)
        {
            char buf[kMaxNumericSize];

            if (auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v,
                                               std::chars_format::general, 12);
                ec == std::errc{})
            {
                buffer_.append(std::span<const char>{buf, ptr});
            }
        }
        return *this;
        // return *this;
    }

    LogStream& operator<<(const void* p) noexcept
    {
        char buf[32];
        // C++20 std::format_to_n 完美替代了原先复杂的 16 进制转换
        auto result = std::format_to_n(buf, sizeof(buf), "{}", p);
        buffer_.append(std::span<const char>{buf, result.out});
        return *this;
    }

    LogStream& operator<<(std::string_view v) noexcept
    {
        buffer_.append(std::span<const char>{v.data(), v.size()});
        return *this;
    }

    LogStream& operator<<(char c) noexcept
    {
        buffer_.append(std::span<const char>{&c, 1});
        return *this;
    }

    LogStream& operator<<(const char* str) noexcept
    {
        if (str)
        {
            buffer_.append(std::string_view{str});
        }
        else
        {
            buffer_.append(std::string_view{"(null)"});
        }
        return *this;
    }

    LogStream& operator<<(const Buffer& buf) noexcept
    {
        buffer_.append(buf.span());
        return *this;
    }

    void append(std::string_view data) noexcept { buffer_.append(data); }

    void append(const char* data, int len) noexcept
    {
        buffer_.append(data, len);
    }

    [[nodiscard]] const Buffer& buffer() const noexcept { return buffer_; }

    void resetBuffer() noexcept { buffer_.reset(); }

    template <typename... Args>
    LogStream& format(std::format_string<Args...> fmt, Args&&... args)
    {
        // 直接将格式化结果写到缓冲区尾部，限制最大写入量防止溢出
        auto result = std::format_to_n(buffer_.current(), buffer_.avail(), fmt,
                                       std::forward<Args>(args)...);
        buffer_.add(result.out - buffer_.current());
        return *this;
    }

private:
    Buffer buffer_;
    static constexpr int kMaxNumericSize = 48;
};

[[nodiscard]] std::string formatSI(int64_t n);

[[nodiscard]] std::string formatIEC(int64_t n);

}  // namespace chaoxi