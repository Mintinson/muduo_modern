#include "chaoxi/net/Buffer.hpp"

#include <array>
#include <cerrno>
#include <cstddef>

#include <sys/uio.h>

namespace chaoxi::net
{

ssize_t Buffer::readFd(int fd, int* savedErrno)
{
    // char extrabuf[65536];
    std::array<char, 65536> extrabuf;
    struct iovec vec[2];

    const std::size_t writable = writableBytes();

    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf.data();
    vec[1].iov_len = extrabuf.size();

    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;

    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)
    {
        *savedErrno = errno;
        return n;
    }

    if (auto written = static_cast<std::size_t>(n); written <= writable)
    {
        writerIndex_ += written;
    }
    else
    {
        writerIndex_ = buffer_.size();
        append(extrabuf.data(), static_cast<std::size_t>(written - writable));
    }

    return n;
}

}  // namespace chaoxi::net