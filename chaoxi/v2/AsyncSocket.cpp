#include "chaoxi/v2/AsyncSocket.hpp"

#include "chaoxi/net/EventLoop.hpp"

#include <cerrno>
#include <system_error>
#include <utility>

#include <netinet/tcp.h>
#include <sys/socket.h>

namespace chaoxi::v2
{
namespace
{

socklen_t addressLength(const net::InetAddress& address) noexcept
{
    return address.family() == AF_INET
               ? static_cast<socklen_t>(sizeof(sockaddr_in))
               : static_cast<socklen_t>(sizeof(sockaddr_in6));
}

}  // namespace

AsyncSocket::AsyncSocket(net::EventLoop& loop, int fd)
    : fd_(loop, fd, FdOwnership::owned)
{
}

Task<AsyncSocket> AsyncSocket::connect(net::EventLoop& loop,
                                       const net::InetAddress& address)
{
    return connectImpl(loop, address);
}

Task<AsyncSocket> AsyncSocket::connectImpl(net::EventLoop& loop,
                                           net::InetAddress address)
{
    loop.assertInLoopThread();
    const int fd =
        ::socket(address.family(), SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                 IPPROTO_TCP);
    if (fd < 0)
    {
        throw std::system_error(errno, std::system_category());
    }

    AsyncSocket socket(loop, fd);
    const int result =
        ::connect(fd, address.getSockAddr(), addressLength(address));
    if (result < 0)
    {
        if (errno != EINPROGRESS && errno != EINTR)
        {
            throw std::system_error(errno, std::system_category());
        }

        std::error_code readinessError;
        try
        {
            co_await socket.fd_.waitWritable();
        }
        catch (const std::system_error& error)
        {
            readinessError = error.code();
        }

        int error = 0;
        socklen_t errorLength = static_cast<socklen_t>(sizeof(error));
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errorLength) < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        if (error != 0)
        {
            throw std::system_error(error, std::system_category());
        }
        if (readinessError)
        {
            throw std::system_error(readinessError);
        }
    }

    co_return std::move(socket);
}

void AsyncSocket::shutdownWrite()
{
    if (::shutdown(nativeHandle(), SHUT_WR) < 0 && errno != ENOTCONN)
    {
        throw std::system_error(errno, std::system_category());
    }
}

void AsyncSocket::setTcpNoDelay(bool enabled)
{
    const int value = enabled ? 1 : 0;
    if (::setsockopt(nativeHandle(), IPPROTO_TCP, TCP_NODELAY, &value,
                     static_cast<socklen_t>(sizeof(value))) < 0)
    {
        throw std::system_error(errno, std::system_category());
    }
}

void AsyncSocket::setKeepAlive(bool enabled)
{
    const int value = enabled ? 1 : 0;
    if (::setsockopt(nativeHandle(), SOL_SOCKET, SO_KEEPALIVE, &value,
                     static_cast<socklen_t>(sizeof(value))) < 0)
    {
        throw std::system_error(errno, std::system_category());
    }
}

}  // namespace chaoxi::v2
