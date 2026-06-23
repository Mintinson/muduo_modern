/**
 * @file LogStream.hpp
 * @author your name (you@domain.com)
 * @brief
 *
 * 优化 1：在之前的 operator<<(integer) or operator<<(floating) 中，虽然使用了
 * std::to_chars 高效实现，但是将结果放入了 char buf[N]
 * 中，在append，多了额外的栈开销以及字符串的拷贝。
 * 优化方法：直接将结果保存在在 buffer_.current() 中，省去了上述步骤，优化极大。
 *
 * 优化 2：对单字符和编译期已知的字符串做了模板特化，允许直接将结果复制到
 * buffer_.current() 中，省去了 strlen 的计算
 *
 */

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace chaoxi
{

namespace detail
{
constexpr size_t kSmallBuffer = 4000;
constexpr size_t kLargeBuffer = 4000 * 1000;

template <std::size_t Size>
class FixedBuffer
{
public:
    FixedBuffer() noexcept : cur_(data_.data()) { setCookie(cookieStart); }

    ~FixedBuffer() { setCookie(cookieEnd); }

    FixedBuffer(const FixedBuffer&) = delete;
    FixedBuffer& operator=(const FixedBuffer&) = delete;

    void append(std::string_view buf) noexcept
    {
        // FIXME: append partially
        if (avail() > buf.size())
        {
            std::memcpy(cur_, buf.data(), buf.size());
            cur_ += buf.size();
        }
    }

    void append(const char* buf, std::size_t len) noexcept
    {
        append({buf, len});
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

    // note: caller should validate len <= avail() before calling it
    [[nodiscard]] std::span<char> writeSpan(std::size_t len) noexcept
    {
        auto* data = cur_;
        add(len);
        return {data, len};
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
        if (buffer_.avail() >= 32)
        {
            // 直接将数字转换到 FixedBuffer 的可用内存中，实现真正零拷贝
            if (auto [ptr, ec] =
                    std::to_chars(buffer_.current(), buffer_.current() + 32, v);
                ec == std::errc{})
            {
                buffer_.add(static_cast<std::size_t>(ptr - buffer_.current()));
            }
        }
        return *this;
    }

    template <std::floating_point T>
    LogStream& operator<<(T v) noexcept
    {
        if (buffer_.avail() >= kMaxNumericSize)
        {
            if (auto [ptr, ec] = std::to_chars(
                    buffer_.current(), buffer_.current() + kMaxNumericSize, v,
                    std::chars_format::general, 12);
                ec == std::errc{})
            {
                buffer_.add(static_cast<std::size_t>(ptr - buffer_.current()));
            }
        }
        return *this;
        // return *this;
    }

    LogStream& operator<<(const void* p) noexcept
    {
        // char buf[32];
        // // C++20 std::format_to_n 完美替代了原先复杂的 16 进制转换
        // auto result = std::format_to_n(buf, sizeof(buf), "{}", p);
        // buffer_.append(std::span<const char>{buf, result.out});
        // return *this;
        if (buffer_.avail() >= kMaxNumericSize)
        {
            auto* buf = buffer_.current();
            buf[0] = '0';
            buf[1] = 'x';
            auto [ptr_end, ec] =
                std::to_chars(buf + 2, buf + kMaxNumericSize,
                              reinterpret_cast<uintptr_t>(p), 16);
            if (ec == std::errc())
            {
                buffer_.add(static_cast<std::size_t>(ptr_end - buf));
            }
        }
        return *this;
    }

    LogStream& operator<<(std::string_view v) noexcept
    {
        // buffer_.append(std::span<const char>{v.data(), v.size()});
        buffer_.append(v);
        return *this;
    }

    LogStream& operator<<(char c) noexcept
    {
        if (buffer_.avail() > 0)
        {
            *buffer_.current() = c;
            buffer_.add(1);
        }
        return *this;
    }

    template <std::size_t N>
    LogStream& operator<<(const char (&str)[N]) noexcept
    {
        buffer_.append(str, N - 1);  // 编译期就确定了长度，扣除 \0
        return *this;
    }

    // for string literals and C-style strings, handle null pointer case
    // use template + concepts to let compile-time raw string call
    // `operator<<(const char (&str)[N])` overload
    template <typename T>
        requires(std::same_as<std::decay_t<T>, const char*> ||
                 std::same_as<std::decay_t<T>, char*>)
    LogStream& operator<<(T str) noexcept
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
        buffer_.append(buf.view());
        return *this;
    }

    void append(std::string_view data) noexcept { buffer_.append(data); }

    void append(const char* data, std::size_t len) noexcept
    {
        buffer_.append(data, len);
    }

    [[nodiscard]] Buffer& buffer() noexcept { return buffer_; }

    [[nodiscard]] const Buffer& buffer() const noexcept { return buffer_; }

    void resetBuffer() noexcept { buffer_.reset(); }

    template <typename... Args>
    LogStream& format(std::format_string<Args...> fmt, Args&&... args)
    {
        // 直接将格式化结果写到缓冲区尾部，限制最大写入量防止溢出
        auto result = std::format_to_n(buffer_.current(),
                                       static_cast<ptrdiff_t>(buffer_.avail()),
                                       fmt, std::forward<Args>(args)...);
        buffer_.add(static_cast<std::size_t>(result.out - buffer_.current()));
        return *this;
    }

private:
    Buffer buffer_;
    static constexpr int kMaxNumericSize = 48;
};

[[nodiscard]] std::string formatSI(int64_t n);

[[nodiscard]] std::string formatIEC(int64_t n);

}  // namespace chaoxi