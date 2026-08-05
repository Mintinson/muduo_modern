#include "chaoxi/net/Buffer.hpp"

#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>

#ifndef _WIN32
#include <sys/uio.h>
#endif

namespace chaoxi::net
{

SignedSize Buffer::readFd(SocketHandle fd, int* savedErrno)
{
#ifdef _WIN32
    const std::size_t writable = writableBytes();
    const int capacity = static_cast<int>(std::min<std::size_t>(writable, INT_MAX));
    const int n = ::recv(fd, beginWrite(), capacity, 0);
    if (n == SOCKET_ERROR)
    {
        *savedErrno = socketErrorToErrno(lastSocketError());
        return -1;
    }
    writerIndex_ += static_cast<std::size_t>(n);
    return n;
#else
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
#endif
}

}  // namespace chaoxi::net
