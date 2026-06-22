#include "chaoxi/base/FileUtil.hpp"

#include <cassert>
#include <cstdio>
#include <print>
#include <string>
#include <string_view>

namespace chaoxi::file_util
{
AppendFile::AppendFile(std::string_view filename)
    : fp_(::fopen(filename.data(), "ae"))  // 'e' for O_CLOEXEC
{
    assert(fp_);
    ::setbuffer(fp_, buffer_.data(), sizeof buffer_);
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
    return ::fwrite_unlocked(logline.data(), 1, logline.size(), fp_);
}
};  // namespace chaoxi::file_util