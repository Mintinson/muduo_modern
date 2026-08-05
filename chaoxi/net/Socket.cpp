#include "chaoxi/net/Socket.hpp"

#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <cstddef>
#include <cstdio>

#ifdef _WIN32
#include <mstcpip.h>
#else
#include <netinet/tcp.h>  // for TCP_INFO, TCP_NODELAY, struct tcp_info
#endif

namespace chaoxi::net
{

Socket::SocketType Socket::accept(InetAddress* peeraddr)
{
    struct sockaddr_in6 addr{};
    SocketType connfd = sockets::accept(sockfd_, &addr);
    if (connfd != kInvalidSocket)
    {
        peeraddr->setSockAddrInet6(addr);
    }
    return connfd;
}

Socket::~Socket()
{
    sockets::close(sockfd_);
}

void Socket::bindAddress(const InetAddress& addr)
{
    sockets::bindOrDie(sockfd_, addr.getSockAddr());
}

void Socket::listen()
{
    sockets::listenOrDie(sockfd_);
}

void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&optval), sizeof optval);
}

void Socket::setReusePort(bool on)
{
    int optval = on ? 1 : 0;
#ifdef SO_REUSEPORT
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT,
                 reinterpret_cast<const char*>(&optval), sizeof optval);
#else
    (void)optval;
#endif
}

void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE,
                 reinterpret_cast<const char*>(&optval), sizeof optval);
}

void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&optval), sizeof optval);
}

void Socket::shutdownWrite()
{
    ::shutdown(sockfd_,
#ifdef _WIN32
               SD_SEND
#else
               SHUT_WR
#endif
    );
}

bool Socket::getTcpInfo(tcp_info* tcpi) const noexcept
{
#ifdef _WIN32
    (void)tcpi;
    return false;
#else
    socklen_t len = sizeof(*tcpi);
    return ::getsockopt(sockfd_, SOL_TCP, TCP_INFO, tcpi, &len) == 0;
#endif
}

bool Socket::getTcpInfoString(char* buf, std::size_t len) const noexcept
{
#ifdef _WIN32
    (void)buf;
    (void)len;
    return false;
#else
    tcp_info tcpi{};
    bool ok = getTcpInfo(&tcpi);
    if (ok)
    {
        // 简单格式化：只输出关键字段
        std::snprintf(buf, len,
                      "unrecovered=%u rtt=%u rttvar=%u snd_cwnd=%u "
                      "rcv_space=%u",
                      tcpi.tcpi_retransmits, tcpi.tcpi_rtt, tcpi.tcpi_rttvar,
                      tcpi.tcpi_snd_cwnd, tcpi.tcpi_rcv_space);
    }
    return ok;
#endif
}

}  // namespace chaoxi::net
