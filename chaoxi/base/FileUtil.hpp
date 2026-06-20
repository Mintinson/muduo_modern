#pragma once
#include <array>
#include <cstddef>
#include <string_view>

#include <sys/types.h>  // for off_t

namespace chaoxi::file_util
{  // not thread safe

class AppendFile
{
    constexpr static std::size_t kBufferSize =
        static_cast<const std::size_t>(64 * 1024);

public:
    explicit AppendFile(const std::string& filename);

    ~AppendFile();

    AppendFile(const AppendFile&) = delete;
    AppendFile(AppendFile&&) = delete;
    AppendFile& operator=(const AppendFile&) = delete;
    AppendFile& operator=(AppendFile&&) = delete;

    void append(std::string_view logline);

    void flush();

    [[nodiscard]] std::size_t writtenBytes() const noexcept
    {
        return writtenBytes_;
    }

private:
    [[nodiscard]] size_t write(std::string_view logline);

    FILE* fp_{nullptr};
    std::array<char, kBufferSize> buffer_{};
    std::size_t writtenBytes_{0};
};
}  // namespace chaoxi::file_util