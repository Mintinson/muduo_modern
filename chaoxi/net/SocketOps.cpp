#include "chaoxi/net/SocketOps.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Endian.hpp"

#include <cassert>
#include <cstring>
#include <format>

#include <sys/socket.h>
#include <sys/uio.h>  // readv
#include <unistd.h>

namespace chaoxi::net::sockets {

ssize_t read(int sockfd, void* buf, size_t count) {
    return ::read(sockfd, buf, count);
}

ssize_t write(int sockfd, const void* buf, size_t count) {
    return ::write(sockfd, buf, count);
}

int getSocketError(int sockfd) {
    int optval{};
    socklen_t optlen = static_cast<socklen_t>(sizeof optval);

    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
        return errno;
    } else {
        return optval;
    }
}

const struct sockaddr* sockaddr_cast(const struct sockaddr_in6* addr) {
    return reinterpret_cast<const struct sockaddr*>(addr);
}

struct sockaddr* sockaddr_cast(struct sockaddr_in6* addr) {
    return reinterpret_cast<struct sockaddr*>(addr);
}

const struct sockaddr* sockaddr_cast(const struct sockaddr_in* addr) {
    return reinterpret_cast<const struct sockaddr*>(addr);
}

const struct sockaddr_in* sockaddr_in_cast(const struct sockaddr* addr) {
    // return static_cast<const struct sockaddr_in*>(implicit_cast<const
    // void*>(addr));
    return reinterpret_cast<const struct sockaddr_in*>(addr);
}

const struct sockaddr_in6* sockaddr_in6_cast(const struct sockaddr* addr) {
    return reinterpret_cast<const struct sockaddr_in6*>(addr);
}

int createNonblockingOrDie(sa_family_t family) {
#if VALGRIND
    int sockfd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        LOG_SYSFATAL << "sockets::createNonblockingOrDie";
    }

    setNonBlockAndCloseOnExec(sockfd);
#else
    int sockfd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          IPPROTO_TCP);
    if (sockfd < 0) {
        LOG_SYSFATAL << "sockets::createNonblockingOrDie";
    }
#endif
    return sockfd;
}

int connect(int sockfd, const struct sockaddr* addr) {
    return ::connect(sockfd, addr, static_cast<socklen_t>(sizeof(*addr)));
}

void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr) {
    addr->sin_family = AF_INET;
    addr->sin_port = hton16(port);
    if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0) {
        LOG_SYSERR << "sockets::fromIpPort";
    }
}

void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in6* addr) {
    addr->sin6_family = AF_INET6;
    addr->sin6_port = hton16(port);
    if (::inet_pton(AF_INET6, ip, &addr->sin6_addr) <= 0) {
        LOG_SYSERR << "sockets::fromIpPort";
    }
}

[[nodiscard]] std::string toIpPort(const sockaddr* addr) {
    if (addr->sa_family == AF_INET6) {
        const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
        uint16_t port = sockets::ntoh16(addr6->sin6_port);

        return std::format("[{}]:{}", toIp(addr), port);
    }

    if (addr->sa_family == AF_INET) {
        const auto* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
        uint16_t port = sockets::ntoh16(addr4->sin_port);

        return std::format("{}:{}", toIp(addr), port);
    }

    return "UnknownFamily:0";
}

void toIpPort(char* buf, size_t size, const struct sockaddr* addr) {
    if (addr->sa_family == AF_INET6) {
        buf[0] = '[';
        toIp(buf + 1, size - 1, addr);
        size_t end = ::strlen(buf);
        const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
        uint16_t port = sockets::ntoh16(addr6->sin6_port);
        assert(size > end);
        std::format_to_n(buf + end, size - end, "]:{}", port);
        return;
    }
    toIp(buf, size, addr);
    size_t end = ::strlen(buf);
    const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
    uint16_t port = sockets::ntoh16(addr4->sin_port);
    assert(size > end);
    std::format_to_n(buf + end, size - end, ":{}", port);
}

void toIp(char* buf, size_t size, const struct sockaddr* addr) {
    if (addr->sa_family == AF_INET) {
        assert(size >= INET_ADDRSTRLEN);
        const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
        ::inet_ntop(AF_INET, &addr4->sin_addr, buf,
                    static_cast<socklen_t>(size));
    } else if (addr->sa_family == AF_INET6) {
        assert(size >= INET6_ADDRSTRLEN);
        const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
        ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf,
                    static_cast<socklen_t>(size));
    }
}

[[nodiscard]] std::string toIp(const sockaddr* addr) {
    if (addr->sa_family == AF_INET) {
        char buf[INET_ADDRSTRLEN];
        const auto* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
        ::inet_ntop(AF_INET, &addr4->sin_addr, buf, sizeof(buf));
        return buf;  // RVO
    }

    if (addr->sa_family == AF_INET6) {
        char buf[INET6_ADDRSTRLEN];
        const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
        ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf, sizeof(buf));
        return buf;
    }

    return "UnknownFamily";
}

void listenOrDie(int sockfd) {
    int ret = ::listen(sockfd, SOMAXCONN);
    if (ret < 0) {
        LOG_SYSFATAL << "sockets::listenOrDie";
    }
}

void bindOrDie(int sockfd, const struct sockaddr* addr) {
    int ret = ::bind(sockfd, addr,
                     static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
    if (ret < 0) {
        LOG_SYSFATAL << "sockets::bindOrDie";
    }
}

int accept(int sockfd, struct sockaddr_in6* addr) {
    socklen_t addrlen = static_cast<socklen_t>(sizeof *addr);
#if VALGRIND || defined(NO_ACCEPT4)
    int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
    setNonBlockAndCloseOnExec(connfd);
#else
    int connfd = ::accept4(sockfd, sockaddr_cast(addr), &addrlen,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
#endif
    if (connfd < 0) {
        int savedErrno = errno;
        LOG_SYSERR << "Socket::accept";
        switch (savedErrno) {
            case EAGAIN:
            case ECONNABORTED:
            case EINTR:
            case EPROTO:  // ???
            case EPERM:
            case EMFILE:  // per-process lmit of open file desctiptor ???
                // expected errors
                errno = savedErrno;
                break;
            case EBADF:
            case EFAULT:
            case EINVAL:
            case ENFILE:
            case ENOBUFS:
            case ENOMEM:
            case ENOTSOCK:
            case EOPNOTSUPP:
                // unexpected errors
                LOG_FATAL << "unexpected error of ::accept " << savedErrno;
                break;
            default:
                LOG_FATAL << "unknown error of ::accept " << savedErrno;
                break;
        }
    }
    return connfd;
}

struct sockaddr_in6 getLocalAddr(int sockfd) {
    struct sockaddr_in6 localaddr{};
    socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
    if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0) {
        LOG_SYSERR << "sockets::getLocalAddr";
    }
    return localaddr;
}

struct sockaddr_in6 getPeerAddr(int sockfd) {
    struct sockaddr_in6 peeraddr{};
    socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
    if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0) {
        LOG_SYSERR << "sockets::getPeerAddr";
    }
    return peeraddr;
}

void close(int sockfd) {
    if (::close(sockfd) < 0) {
        LOG_SYSERR << "sockets::close";
    }
}

// ============================================================================
// sockets::isSelfConnect —— 检测"自连接"的工具函数
// ============================================================================

///
/// @brief 检测 socket 是否"自连接"
///
/// 什么是自连接（self-connect）？
///   非阻塞 connect 在内核中立即完成了 TCP 三次握手，
///   但客户端和服务端使用了相同的 IP:port。
///   这通常发生在客户端绑定了一个本地端口后又连接到同一端口。
///
/// 检测方法：比较 sockfd 的 local 和 peer 地址是否相同。
///
/// @return true 如果是自连接（此时应丢弃该 socket，重试）
///
bool isSelfConnect(int sockfd)
{
    struct sockaddr_in6 localaddr = getLocalAddr(sockfd);
    struct sockaddr_in6 peeraddr = getPeerAddr(sockfd);
    if (localaddr.sin6_family == AF_INET)
    {
        const struct sockaddr_in* laddr4 =
            reinterpret_cast<struct sockaddr_in*>(&localaddr);
        const struct sockaddr_in* raddr4 =
            reinterpret_cast<struct sockaddr_in*>(&peeraddr);
        return laddr4->sin_port == raddr4->sin_port &&
               laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;
    }
    if (localaddr.sin6_family == AF_INET6)
    {
        return localaddr.sin6_port == peeraddr.sin6_port &&
               memcmp(&localaddr.sin6_addr, &peeraddr.sin6_addr,
                      sizeof localaddr.sin6_addr) == 0;
    }
    return false;
}

}  // namespace chaoxi::net::sockets
