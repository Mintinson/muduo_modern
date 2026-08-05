#include "chaoxi/v2/AsyncSocket.hpp"

#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <cerrno>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <mstcpip.h>
#else
#include <netinet/tcp.h>
#endif

namespace chaoxi::v2
{
AsyncSocket::AsyncSocket(net::EventLoop& loop, net::SocketHandle fd)
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
    const net::SocketHandle fd =
        net::sockets::createNonblocking(address.family());
    if (fd == net::kInvalidSocket)
    {
        throw std::system_error(errno, std::generic_category());
    }

    AsyncSocket socket(loop, fd);
    const int result = net::sockets::connect(fd, address.getSockAddr());
    if (result < 0)
    {
        if (errno != EINPROGRESS && errno != EINTR)
        {
            throw std::system_error(errno, std::generic_category());
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

        const int error = net::sockets::getSocketError(fd);
        if (error != 0)
        {
            throw std::system_error(error, std::generic_category());
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
    if (net::sockets::shutdownWrite(nativeHandle()) < 0 && errno != ENOTCONN)
    {
        throw std::system_error(errno, std::generic_category());
    }
}

void AsyncSocket::setTcpNoDelay(bool enabled)
{
    const int value = enabled ? 1 : 0;
    if (net::sockets::setSocketOption(nativeHandle(), IPPROTO_TCP, TCP_NODELAY,
                                      value) < 0)
    {
        throw std::system_error(errno, std::generic_category());
    }
}

void AsyncSocket::setKeepAlive(bool enabled)
{
    const int value = enabled ? 1 : 0;
    if (net::sockets::setSocketOption(nativeHandle(), SOL_SOCKET, SO_KEEPALIVE,
                                      value) < 0)
    {
        throw std::system_error(errno, std::generic_category());
    }
}

}  // namespace chaoxi::v2
