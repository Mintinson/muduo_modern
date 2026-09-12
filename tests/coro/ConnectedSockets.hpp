#pragma once

#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/Platform.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <array>
#include <cerrno>
#include <system_error>
#include <utility>

namespace chaoxi::coro::test
{

/// A connected TCP socket pair that works on both POSIX and Windows.
///
/// AF_UNIX socketpair() is convenient on Linux but is not uniformly available
/// on supported Windows SDKs. A loopback TCP connection exercises the same
/// readiness and backpressure paths as production sockets.
class ConnectedSockets
{
public:
    ConnectedSockets()
    {
        net::ensureNetworkInitialized();
        net::SocketHandle listener =
            ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == net::kInvalidSocket)
        {
            throwLastSocketError();
        }

        try
        {
            const net::InetAddress loopback{0, true};
            if (net::sockets::bind(listener, loopback.getSockAddr()) < 0 ||
                net::sockets::listen(listener) < 0)
            {
                throwErrno();
            }

            handles_[1] = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (handles_[1] == net::kInvalidSocket)
            {
                throwLastSocketError();
            }

            const sockaddr_in6 address = net::sockets::getLocalAddr(listener);
            if (net::sockets::connect(
                    handles_[1], reinterpret_cast<const sockaddr*>(&address)) <
                0)
            {
                throwErrno();
            }

            sockaddr_in6 peer{};
            net::SocketLength length =
                static_cast<net::SocketLength>(sizeof(peer));
            handles_[0] = ::accept(
                listener, reinterpret_cast<sockaddr*>(&peer), &length);
            if (handles_[0] == net::kInvalidSocket)
            {
                throwLastSocketError();
            }

            if (net::sockets::setNonblocking(handles_[0]) < 0)
            {
                throwErrno();
            }
        }
        catch (...)
        {
            closeIfOpen(listener);
            closeIfOpen(handles_[0]);
            closeIfOpen(handles_[1]);
            throw;
        }
        closeIfOpen(listener);
    }

    ~ConnectedSockets()
    {
        closeIfOpen(handles_[0]);
        closeIfOpen(handles_[1]);
    }

    ConnectedSockets(const ConnectedSockets&) = delete;
    ConnectedSockets& operator=(const ConnectedSockets&) = delete;

    [[nodiscard]] net::SocketHandle first() const noexcept
    {
        return handles_[0];
    }

    [[nodiscard]] net::SocketHandle second() const noexcept
    {
        return handles_[1];
    }

    [[nodiscard]] net::SocketHandle releaseFirst() noexcept
    {
        return std::exchange(handles_[0], net::kInvalidSocket);
    }

    void closeSecond() noexcept { closeIfOpen(handles_[1]); }

private:
    [[noreturn]] static void throwErrno()
    {
        throw std::system_error(errno, std::generic_category());
    }

    [[noreturn]] static void throwLastSocketError()
    {
#ifdef _WIN32
        errno = net::socketErrorToErrno(net::lastSocketError());
#endif
        throwErrno();
    }

    static void closeIfOpen(net::SocketHandle& handle) noexcept
    {
        if (handle != net::kInvalidSocket)
        {
            net::sockets::close(handle);
            handle = net::kInvalidSocket;
        }
    }

    std::array<net::SocketHandle, 2> handles_{
        net::kInvalidSocket, net::kInvalidSocket};
};

}  // namespace chaoxi::coro::test
