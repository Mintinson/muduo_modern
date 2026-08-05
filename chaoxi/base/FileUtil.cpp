#include "chaoxi/base/FileUtil.hpp"

#include <cassert>
#include <cstdio>
#include <print>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <share.h>
#endif
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace chaoxi::file_util
{
namespace
{
#ifndef _WIN32
class UniqueFileDescription
{
public:
    explicit UniqueFileDescription(int fd) noexcept : fd_(fd) {}

    UniqueFileDescription(UniqueFileDescription&& other) noexcept
        : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    UniqueFileDescription& operator=(UniqueFileDescription&& other) noexcept
    {
        if (this != &other)
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    UniqueFileDescription(UniqueFileDescription&) = delete;
    UniqueFileDescription& operator=(UniqueFileDescription&) = delete;

    ~UniqueFileDescription()
    {
        if (fd_ >= 0)
        {
            int ret{};
            // 解决原版 FIXME: 遇到系统信号中断时自动重试关闭，防止 FD 泄漏
            do
            {
                ret = ::close(fd_);
            } while (ret < 0 && errno == EINTR);
        }
    }

    UniqueFileDescription(const UniqueFileDescription&) = delete;
    UniqueFileDescription& operator=(const UniqueFileDescription&) = delete;

    [[nodiscard]] int get() const noexcept { return fd_; }

    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

private:
    int fd_;
};
#endif

}  // namespace

[[nodiscard]] std::expected<ReadResult, std::error_code> readSmallFile(
    const std::filesystem::path& filename, size_t maxSize)
{
#ifdef _WIN32
    std::error_code ec;
    if (std::filesystem::is_directory(filename, ec))
    {
        return std::unexpected(std::make_error_code(std::errc::is_a_directory));
    }
    const auto size = std::filesystem::file_size(filename, ec);
    if (ec)
    {
        return std::unexpected(ec);
    }
    FILE* file = nullptr;
    if (::_wfopen_s(&file, filename.c_str(), L"rb") != 0 || file == nullptr)
    {
        return std::unexpected(std::error_code(errno, std::system_category()));
    }
    ReadResult result;
    result.meta.fileSize = static_cast<std::size_t>(size);
    const auto writeTime = std::filesystem::last_write_time(filename, ec);
    if (!ec)
    {
        result.meta.modifyTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            writeTime - decltype(writeTime)::clock::now() + std::chrono::system_clock::now());
        result.meta.createTime = result.meta.modifyTime;
    }
    const auto toRead = std::min(maxSize, result.meta.fileSize);
    result.content.resize(toRead);
    result.content.resize(::fread(result.content.data(), 1, toRead, file));
    (void)::fclose(file);
    return result;
#else
    UniqueFileDescription fd(::open(filename.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.valid())
    {
        return std::unexpected(std::error_code(errno, std::system_category()));
    }

    struct stat statbuf{};
    if (::fstat(fd.get(), &statbuf) != 0)
    {
        return std::unexpected(std::error_code(errno, std::system_category()));
    }

    if (S_ISDIR(statbuf.st_mode))
    {
        return std::unexpected(std::error_code(EISDIR, std::system_category()));
    }
    ReadResult result;
    result.meta.fileSize = static_cast<std::size_t>(statbuf.st_size);
    result.meta.modifyTime =
        std::chrono::system_clock::from_time_t(statbuf.st_mtime);
    result.meta.createTime =
        std::chrono::system_clock::from_time_t(statbuf.st_ctime);

    if (S_ISREG(statbuf.st_mode) && statbuf.st_size > 0)
    {
        // 常规文件：已知大小，直接一次性分配并读取
        size_t toRead = std::min(maxSize, result.meta.fileSize);

        // C++23 resize_and_overwrite: 直接将底层未初始化的内存交给 read
        // 填充，消除所有临时 buffer 和 append 拷贝！
        result.content.resize_and_overwrite(
            toRead,
            [&](char* buf, size_t n)
            {
                size_t totalRead = 0;
                while (totalRead < n)
                {
                    ssize_t bytes =
                        ::read(fd.get(), buf + totalRead, n - totalRead);
                    if (bytes > 0)
                    {
                        totalRead += static_cast<std::size_t>(bytes);
                    }
                    else if (bytes == 0)
                    {
                        break;  // EOF
                    }
                    else
                    {
                        if (errno == EINTR)
                        {
                            continue;  // 遇到信号打断则重试
                        }
                        break;
                    }
                }
                return totalRead;  // 告诉 string 实际填充了多少字节
            });
    }
    else
    {
        // 特殊文件 (如 /proc/cpuinfo)：st_size 通常为 0，不能预分配
        // 动态分块读取，直接向 string 底层写入，依然彻底干掉类内的 64KB 栈内存！
        size_t currentSize = 0;
        while (currentSize < maxSize)
        {
            size_t chunk =
                std::min(maxSize - currentSize, static_cast<size_t>(64 * 1024));
            result.content.resize(currentSize + chunk);

            ssize_t bytes =
                ::read(fd.get(), result.content.data() + currentSize, chunk);
            if (bytes > 0)
            {
                currentSize += static_cast<std::size_t>(bytes);
            }
            else if (bytes == 0)
            {
                break;
            }
            else
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
        }
        result.content.resize(currentSize);  // 收缩到真实读取的大小
    }

    return result;
#endif
}

AppendFile::AppendFile(std::string_view filename)
#ifdef _WIN32
    : fp_(nullptr)
#else
    : fp_(::fopen(filename.data(), "ae"))  // 'e' for O_CLOEXEC
#endif
{
#ifdef _WIN32
    fp_ = ::_fsopen(std::string(filename).c_str(), "ab", _SH_DENYNO);
#endif
    assert(fp_);
    (void)::setvbuf(fp_, buffer_.data(), _IOFBF, sizeof buffer_);
    // posix_fadvise POSIX_FADV_DONTNEED ?
}

AppendFile::~AppendFile()
{
    (void)::fclose(fp_);
}

void AppendFile::append(std::string_view logline)
{
    size_t written = 0;

    while (written != logline.size())
    {
        auto remain = logline.size() - written;
        auto n = write(logline.substr(written, remain));

        if (n != remain)
        {
            int err = ferror(fp_);
            if (err != 0)
            {
                std::error_code ec(err, std::system_category());
                std::println(stderr, "AppendFile::append() failed {}",
                             ec.message());

                break;
            }
        }
        written += n;
    }
    writtenBytes_ += written;
}

void AppendFile::flush()
{
    (void)::fflush(fp_);
}

size_t AppendFile::write(std::string_view logline)
{
    // #undef fwrite_unlocked
    // thread-unsafe
#ifdef _WIN32
    return ::fwrite(logline.data(), 1, logline.size(), fp_);
#else
    return ::fwrite_unlocked(logline.data(), 1, logline.size(), fp_);
#endif
}
};  // namespace chaoxi::file_util
