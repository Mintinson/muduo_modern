#ifndef MUDUO_BASE_GZIPFILE_HPP
#define MUDUO_BASE_GZIPFILE_HPP

#include "chaoxi/base/Noncopyable.hpp"

#include <string_view>
#include <utility>

#include <sys/types.h>
#include <zlib.h>

namespace chaoxi
{

class GzipFile : NonCopyable
{
public:
    GzipFile(GzipFile&& rhs) noexcept : file_{rhs.file_} { rhs.file_ = nullptr; }

    ~GzipFile()
    {
        if (file_)
        {
            ::gzclose(file_);
        }
    }

    GzipFile& operator=(GzipFile&& rhs) noexcept
    {
        swap(rhs);
        return *this;
    }

    bool valid() const { return file_ != nullptr; }

    void swap(GzipFile& rhs) { std::swap(file_, rhs.file_); }

#if ZLIBNG_VERNUM >= 0x1240
    bool setBuffer(int size) { return ::gzbuffer(file_, size) == 0; }
#endif

    // return the number of uncompressed bytes actually read, 0 for eof, -1 for
    // error
    int read(void* buf, int len) { return ::gzread(file_, buf, len); }

    // return the number of uncompressed bytes actually written
    int write(std::string_view buf)
    {
        return ::gzwrite(file_, buf.data(), buf.size());
    }

    // number of uncompressed bytes
    off_t tell() const { return ::gztell(file_); }

#if ZLIB_VERNUM >= 0x1240
    // number of compressed bytes
    off_t offset() const { return ::gzoffset(file_); }
#endif

    static GzipFile openForRead(std::string_view filename)
    {
        return GzipFile(::gzopen(filename.data(), "rbe"));
    }

    static GzipFile openForAppend(std::string_view filename)
    {
        return GzipFile(::gzopen(filename.data(), "abe"));
    }

    static GzipFile openForWriteExclusive(std::string_view filename)
    {
        return GzipFile(::gzopen(filename.data(), "wbxe"));
    }

    static GzipFile openForWriteTruncate(std::string_view filename)
    {
        return GzipFile(::gzopen(filename.data(), "wbe"));
    }

private:
    explicit GzipFile(gzFile file) : file_(file) {}

    gzFile file_;
};

}  // namespace chaoxi

#endif  // MUDUO_BASGZIPFILE_HPPPP
