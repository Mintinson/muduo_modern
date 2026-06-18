#include "chaoxi/net/Socket.hpp"

#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <cstdio>
#include <netinet/tcp.h>  // for TCP_INFO, TCP_NODELAY, struct tcp_info

namespace chaoxi::net {

int Socket::accept(InetAddress* peeraddr) {
    struct sockaddr_in6 addr{};
    int connfd = sockets::accept(sockfd_, &addr);
    if (connfd >= 0) {
        peeraddr->setSockAddrInet6(addr);
    }
    return connfd;
}

Socket::~Socket() {
    sockets::close(sockfd_);
}

void Socket::bindAddress(const InetAddress& addr) {
    sockets::bindOrDie(sockfd_, addr.getSockAddr());
}

void Socket::listen() {
    sockets::listenOrDie(sockfd_);
}

void Socket::setReuseAddr(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval,
                 static_cast<socklen_t>(sizeof optval));
}

void Socket::setReusePort(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval,
                 static_cast<socklen_t>(sizeof optval));
}

void Socket::setKeepAlive(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval,
                 static_cast<socklen_t>(sizeof optval));
}

void Socket::setTcpNoDelay(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval,
                 static_cast<socklen_t>(sizeof optval));
}

void Socket::shutdownWrite() {
    ::shutdown(sockfd_, SHUT_WR);
}

bool Socket::getTcpInfo(tcp_info* tcpi) const noexcept {
    socklen_t len = sizeof(*tcpi);
    return ::getsockopt(sockfd_, SOL_TCP, TCP_INFO, tcpi, &len) == 0;
}

bool Socket::getTcpInfoString(char* buf, int len) const noexcept {
    tcp_info tcpi{};
    bool ok = getTcpInfo(&tcpi);
    if (ok) {
        // 简单格式化：只输出关键字段
        std::snprintf(buf, len,
                      "unrecovered=%u rtt=%u rttvar=%u snd_cwnd=%u "
                      "rcv_space=%u",
                      tcpi.tcpi_retransmits,
                      tcpi.tcpi_rtt,
                      tcpi.tcpi_rttvar,
                      tcpi.tcpi_snd_cwnd,
                      tcpi.tcpi_rcv_space);
    }
    return ok;
}

}  // namespace chaoxi::net