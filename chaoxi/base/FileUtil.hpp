#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string_view>

namespace chaoxi::file_util
{  // 本命名空间中的文件对象本身不提供线程安全保证

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

/// 读取小文件及其元数据。
/// @param filename 文件路径。
/// @param maxSize 最多读取的字节数。
/// @return 成功时返回内容和元数据，失败时返回系统错误码。
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

    /// 刷新用户态缓冲，并请求操作系统将文件内容同步到存储设备。
    [[nodiscard]] bool sync();

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
