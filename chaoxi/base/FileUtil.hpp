#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string_view>


namespace chaoxi::file_util
{  // not thread safe

struct FileMetaData
{
    size_t fileSize = 0;
    std::chrono::system_clock::time_point modifyTime;
    std::chrono::system_clock::time_point createTime;
};

struct ReadResult
{
    std::string content;
    FileMetaData meta;
};

/**
 * @brief
 *
 * @param filename
 * @param maxSize
 * @return std::expected<ReadResult, std::error_code>
 */
[[nodiscard]] std::expected<ReadResult, std::error_code> readSmallFile(
    const std::filesystem::path& filename, size_t maxSize = 64 * 1024);



class AppendFile
{
    constexpr static std::size_t kBufferSize =
        static_cast<std::size_t>(64 * 1024);

public:
    explicit AppendFile(std::string_view filename);

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
