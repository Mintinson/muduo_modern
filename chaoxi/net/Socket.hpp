#pragma once

#include "chaoxi/net/Platform.hpp"

#include <cstddef>
struct tcp_info;

namespace chaoxi::net
{
class InetAddress;

class Socket
{
public:
    using SocketType = SocketHandle;

    explicit Socket(SocketType fd) : sockfd_(fd) {}

    ~Socket();

    [[nodiscard]] SocketType fd() const noexcept { return sockfd_; }

    // return true if success.
    [[nodiscard]] bool getTcpInfo(struct tcp_info*) const noexcept;
    [[nodiscard]] bool getTcpInfoString(char* buf,
                                        std::size_t len) const noexcept;

    /// abort if address in use
    void bindAddress(const InetAddress& localaddr);
    /// abort if address in use
    void listen();

    /// On success, returns a non-negative integer that is
    /// a descriptor for the accepted socket, which has been
    /// set to non-blocking and close-on-exec. *peeraddr is assigned.
    /// On error, -1 is returned, and *peeraddr is untouched.
    [[nodiscard]] SocketType accept(InetAddress* peeraddr);

    void shutdownWrite();

    ///
    /// Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
    ///
    void setTcpNoDelay(bool on);

    ///
    /// Enable/disable SO_REUSEADDR
    ///
    void setReuseAddr(bool on);

    ///
    /// Enable/disable SO_REUSEPORT
    ///
    void setReusePort(bool on);

    ///
    /// Enable/disable SO_KEEPALIVE
    ///
    void setKeepAlive(bool on);

private:
    const SocketType sockfd_;
};
}  // namespace chaoxi::net
