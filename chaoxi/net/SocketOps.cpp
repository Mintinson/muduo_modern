///
/// @file SocketOps.cpp
/// @brief 裸 socket 系统调用的实现 —— 每个函数对应一个 POSIX 系统调用
///

#include "chaoxi/net/SocketOps.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Endian.hpp"

#include <cassert>
#include <algorithm>
#include <climits>
#include <cstring>
#include <format>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace chaoxi::net::sockets
{

/// @brief 封装 ::read
SignedSize read(SocketHandle sockfd, void* buf, size_t count)
{
#ifdef _WIN32
    int result = ::recv(sockfd, static_cast<char*>(buf),
                        static_cast<int>(std::min<std::size_t>(count, INT_MAX)), 0);
    if (result == SOCKET_ERROR)
    {
        errno = socketErrorToErrno(lastSocketError());
        return -1;
    }
    return result;
#else
    return ::read(sockfd, buf, count);
#endif
}

/// @brief 封装 ::write
SignedSize write(SocketHandle sockfd, const void* buf, size_t count)
{
#ifdef _WIN32
    int result = ::send(sockfd, static_cast<const char*>(buf),
                        static_cast<int>(std::min<std::size_t>(count, INT_MAX)), 0);
    if (result == SOCKET_ERROR)
    {
        errno = socketErrorToErrno(lastSocketError());
        return -1;
    }
    return result;
#else
    return ::write(sockfd, buf, count);
#endif
}

/// @brief 通过 getsockopt(SO_ERROR) 获取 socket 的 pending 错误
///
/// SO_ERROR 被 getsockopt 读取后会被内核自动清零。
/// 如果 getsockopt 本身出错（参数非法），返回 errno。
///
int getSocketError(SocketHandle sockfd)
{
    int optval{};
    SocketLength optlen = static_cast<SocketLength>(sizeof optval);

    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
#ifdef _WIN32
                     reinterpret_cast<char*>(&optval),
#else
                     &optval,
#endif
                     &optlen) < 0)
    {
        return socketErrorToErrno(lastSocketError());
    }
    else
    {
        return socketErrorToErrno(optval);
    }
}

// ---- sockaddr 类型安全转换 ----
// 将各种 struct sockaddr_* 之间的 reinterpret_cast 统一托管，减少 UB 风险

const struct sockaddr* sockaddr_cast(const struct sockaddr_in6* addr)
{
    return reinterpret_cast<const struct sockaddr*>(addr);
}

struct sockaddr* sockaddr_cast(struct sockaddr_in6* addr)
{
    return reinterpret_cast<struct sockaddr*>(addr);
}

const struct sockaddr* sockaddr_cast(const struct sockaddr_in* addr)
{
    return reinterpret_cast<const struct sockaddr*>(addr);
}

const struct sockaddr_in* sockaddr_in_cast(const struct sockaddr* addr)
{
    return reinterpret_cast<const struct sockaddr_in*>(addr);
}

const struct sockaddr_in6* sockaddr_in6_cast(const struct sockaddr* addr)
{
    return reinterpret_cast<const struct sockaddr_in6*>(addr);
}

/// @brief 创建非阻塞 + close-on-exec 的 TCP socket
///
/// 现代 Linux（2.6.27+）支持 SOCK_NONBLOCK | SOCK_CLOEXEC 作为 socket() 标志，
/// 避免了传统方式中先 socket() 再 fcntl(F_SETFL) 的两次调用与竞态条件。
///
SocketHandle createNonblocking(sa_family_t family) noexcept
{
#ifdef _WIN32
    ensureNetworkInitialized();
    SocketHandle sockfd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == kInvalidSocket)
    {
        errno = socketErrorToErrno(lastSocketError());
        return kInvalidSocket;
    }
    if (setNonblocking(sockfd) < 0)
    {
        const int savedErrno = errno;
        ::closesocket(sockfd);
        errno = savedErrno;
        return kInvalidSocket;
    }
#elif defined(VALGRIND)
    int sockfd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0)
    {
        return kInvalidSocket;
    }
    if (setNonblocking(sockfd) < 0)
    {
        const int savedErrno = errno;
        ::close(sockfd);
        errno = savedErrno;
        return kInvalidSocket;
    }
#else
    int sockfd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          IPPROTO_TCP);
#endif
    return sockfd;
}

int setNonblocking(SocketHandle sockfd) noexcept
{
#ifdef _WIN32
    u_long enabled = 1;
    if (::ioctlsocket(sockfd, FIONBIO, &enabled) == SOCKET_ERROR)
    {
        errno = socketErrorToErrno(lastSocketError());
        return -1;
    }
    return 0;
#else
    const int statusFlags = ::fcntl(sockfd, F_GETFL, 0);
    if (statusFlags < 0 ||
        ::fcntl(sockfd, F_SETFL, statusFlags | O_NONBLOCK) < 0)
    {
        return -1;
    }

    const int descriptorFlags = ::fcntl(sockfd, F_GETFD, 0);
    if (descriptorFlags < 0 ||
        ::fcntl(sockfd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0)
    {
        return -1;
    }
    return 0;
#endif
}

SocketHandle createNonblockingOrDie(sa_family_t family)
{
    const SocketHandle sockfd = createNonblocking(family);
    if (sockfd == kInvalidSocket)
    {
        LOG_SYSFATAL << "sockets::createNonblockingOrDie";
    }
    return sockfd;
}

/// @brief 非阻塞 connect
///
/// 当返回 -1 且 errno == EINPROGRESS 时是正常情况：
/// 内核正在后台进行 TCP 三次握手，后续通过 epoll 可写事件检测连接完成。
///
int connect(SocketHandle sockfd, const struct sockaddr* addr)
{
    const int length = addr->sa_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    int result = ::connect(sockfd, addr, static_cast<SocketLength>(length));
#ifdef _WIN32
    if (result == SOCKET_ERROR)
    {
        const int error = lastSocketError();
        errno = error == WSAEWOULDBLOCK ? EINPROGRESS : socketErrorToErrno(error);
        return -1;
    }
#endif
    return result;
}

int bind(SocketHandle sockfd, const struct sockaddr* addr) noexcept
{
    const SocketLength length = static_cast<SocketLength>(
        addr->sa_family == AF_INET6 ? sizeof(sockaddr_in6)
                                    : sizeof(sockaddr_in));
    const int result = ::bind(sockfd, addr, length);
#ifdef _WIN32
    if (result == SOCKET_ERROR)
    {
        errno = socketErrorToErrno(lastSocketError());
        return -1;
    }
#endif
    return result;
}

int listen(SocketHandle sockfd) noexcept
{
    const int result = ::listen(sockfd, SOMAXCONN);
#ifdef _WIN32
    if (result == SOCKET_ERROR)
    {
        errno = socketErrorToErrno(lastSocketError());
        return -1;
    }
#endif
    return result;
}

int setSocketOption(SocketHandle sockfd,
                    int level,
                    int option,
                    int value) noexcept
{
    const int result = ::setsockopt(
        sockfd, level, option, reinterpret_cast<const char*>(&value),
        static_cast<SocketLength>(sizeof(value)));
#ifdef _WIN32
    if (result == SOCKET_ERROR)
    {
        errno = socketErrorToErrno(lastSocketError());
        return -1;
    }
#endif
    return result;
}

int shutdownWrite(SocketHandle sockfd) noexcept
{
    const int result = ::shutdown(
        sockfd,
#ifdef _WIN32
        SD_SEND
#else
        SHUT_WR
#endif
    );
#ifdef _WIN32
    if (result == SOCKET_ERROR)
    {
        errno = socketErrorToErrno(lastSocketError());
        return -1;
    }
#endif
    return result;
}

/// @brief 将 IP:port 字符串解析为 sockaddr_in（IPv4）
void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr)
{
    addr->sin_family = AF_INET;
    addr->sin_port = hton16(port);  // host → network 字节序
    if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
    {
        LOG_SYSERR << "sockets::fromIpPort";
    }
}

/// @brief 将 IP:port 字符串解析为 sockaddr_in6（IPv6）
void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in6* addr)
{
    addr->sin6_family = AF_INET6;
    addr->sin6_port = hton16(port);
    if (::inet_pton(AF_INET6, ip, &addr->sin6_addr) <= 0)
    {
        LOG_SYSERR << "sockets::fromIpPort";
    }
}

/// @brief 将 sockaddr 格式化为 "[IPv6]:port" 或 "IPv4:port" 字符串
///
/// IPv6 地址用方括号括起以消除 port 分隔歧义，如 [::1]:80。
///
[[nodiscard]] std::string toIpPort(const sockaddr* addr)
{
    if (addr->sa_family == AF_INET6)
    {
        const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
        uint16_t port = sockets::ntoh16(addr6->sin6_port);
        return std::format("[{}]:{}", toIp(addr), port);
    }
    if (addr->sa_family == AF_INET)
    {
        const auto* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
        uint16_t port = sockets::ntoh16(addr4->sin_port);
        return std::format("{}:{}", toIp(addr), port);
    }
    return "UnknownFamily:0";
}

/// @brief toIpPort 的缓冲区版本（C 风格接口）
void toIpPort(char* buf, size_t size, const struct sockaddr* addr)
{
    if (addr->sa_family == AF_INET6)
    {
        char ip[INET6_ADDRSTRLEN]{};
        toIp(ip, sizeof ip, addr);
        const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
        uint16_t port = sockets::ntoh16(addr6->sin6_port);
        const auto text = std::format("[{}]:{}", ip, port);
        const auto count = std::min(size > 0 ? size - 1 : 0, text.size());
        if (size > 0)
        {
            std::memcpy(buf, text.data(), count);
            buf[count] = '\0';
        }
        return;
    }
    toIp(buf, size, addr);
    size_t end = ::strlen(buf);
    const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
    uint16_t port = sockets::ntoh16(addr4->sin_port);
    const auto suffix = std::format(":{}", port);
    const auto count = std::min(size > end ? size - end - 1 : 0, suffix.size());
    if (size > end)
    {
        std::memcpy(buf + end, suffix.data(), count);
        buf[end + count] = '\0';
    }
}

/// @brief 从 sockaddr 提取 IP 地址字符串（缓冲区版本）
void toIp(char* buf, size_t size, const struct sockaddr* addr)
{
    if (addr->sa_family == AF_INET)
    {
        assert(size >= INET_ADDRSTRLEN);
        const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
        ::inet_ntop(AF_INET, &addr4->sin_addr, buf,
                    static_cast<SocketLength>(size));
    }
    else if (addr->sa_family == AF_INET6)
    {
        assert(size >= INET6_ADDRSTRLEN);
        const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
        ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf,
                    static_cast<SocketLength>(size));
    }
}

/// @brief 从 sockaddr 提取 IP 地址字符串（返回 std::string）
[[nodiscard]] std::string toIp(const sockaddr* addr)
{
    if (addr->sa_family == AF_INET)
    {
        char buf[INET_ADDRSTRLEN];
        const auto* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
        ::inet_ntop(AF_INET, &addr4->sin_addr, buf, sizeof(buf));
        return buf;  // RVO
    }
    if (addr->sa_family == AF_INET6)
    {
        char buf[INET6_ADDRSTRLEN];
        const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
        ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf, sizeof(buf));
        return buf;
    }
    return "UnknownFamily";
}

/// @brief 开始监听，backlog = SOMAXCONN（系统上限）
void listenOrDie(SocketHandle sockfd)
{
    if (listen(sockfd) < 0)
    {
        LOG_SYSFATAL << "sockets::listenOrDie";
    }
}

/// @brief bind 到指定地址，失败 fatal
void bindOrDie(SocketHandle sockfd, const struct sockaddr* addr)
{
    if (bind(sockfd, addr) < 0)
    {
        LOG_SYSFATAL << "sockets::bindOrDie";
    }
}

/// @brief 非阻塞 accept，使用 accept4 一次完成 SOCK_NONBLOCK | SOCK_CLOEXEC
///
/// accept 失败时会根据 errno 分类处理:
///   - 期望的错误（EAGAIN, ECONNABORTED, EINTR, ...）→ 仅日志，返回 -1
///   - 非期望的错误（EBADF, EFAULT, ...）→ LOG_FATAL，终止程序
///
SocketHandle accept(SocketHandle sockfd, struct sockaddr_in6* addr)
{
    SocketLength addrlen = static_cast<SocketLength>(sizeof *addr);
#if defined(_WIN32)
    SocketHandle connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
    if (connfd != kInvalidSocket)
    {
        if (setNonblocking(connfd) < 0)
        {
            const int savedErrno = errno;
            ::closesocket(connfd);
            errno = savedErrno;
            return kInvalidSocket;
        }
    }
#elif defined(VALGRIND) || defined(NO_ACCEPT4)
    SocketHandle connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
    if (connfd != kInvalidSocket && setNonblocking(connfd) < 0)
    {
        const int savedErrno = errno;
        ::close(connfd);
        errno = savedErrno;
        return kInvalidSocket;
    }
#else
    SocketHandle connfd = ::accept4(sockfd, sockaddr_cast(addr), &addrlen,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
#endif
    if (connfd == kInvalidSocket)
    {
#ifdef _WIN32
        errno = socketErrorToErrno(lastSocketError());
#endif
        const int savedErrno = errno;
        switch (savedErrno)
        {
            case EAGAIN:
#if EWOULDBLOCK != EAGAIN
            case EWOULDBLOCK:
#endif
                // Draining a nonblocking listening socket normally ends here.
                errno = savedErrno;
                break;
            case ECONNABORTED:
            case EINTR:
            case EPROTO:
            case EPERM:
            case EMFILE:
                // 期望的错误，errno 保留给调用者
                LOG_SYSERR << "Socket::accept";
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
                // 非期望错误，终止程序
                errno = savedErrno;
                LOG_FATAL << "unexpected error of ::accept " << savedErrno;
                break;
            default:
                errno = savedErrno;
                LOG_FATAL << "unknown error of ::accept " << savedErrno;
                break;
        }
    }
    return connfd;
}

/// @brief 获取本地地址（通过 getsockname）
struct sockaddr_in6 getLocalAddr(SocketHandle sockfd)
{
    struct sockaddr_in6 localaddr{};
    SocketLength addrlen = static_cast<SocketLength>(sizeof localaddr);
    if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0)
    {
#ifdef _WIN32
        errno = socketErrorToErrno(lastSocketError());
#endif
        LOG_SYSERR << "sockets::getLocalAddr";
    }
    return localaddr;
}

/// @brief 获取对端地址（通过 getpeername）
struct sockaddr_in6 getPeerAddr(SocketHandle sockfd)
{
    struct sockaddr_in6 peeraddr{};
    SocketLength addrlen = static_cast<SocketLength>(sizeof peeraddr);
    if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0)
    {
#ifdef _WIN32
        errno = socketErrorToErrno(lastSocketError());
#endif
        LOG_SYSERR << "sockets::getPeerAddr";
    }
    return peeraddr;
}

/// @brief 关闭 fd（封装 ::close，失败记录日志）
void close(SocketHandle sockfd)
{
#ifdef _WIN32
    if (::closesocket(sockfd) == SOCKET_ERROR)
#else
    if (::close(sockfd) < 0)
#endif
    {
#ifdef _WIN32
        errno = socketErrorToErrno(lastSocketError());
#endif
        LOG_SYSERR << "sockets::close";
    }
}

/// @brief 检测是否"自连接" — local 与 peer 地址完全相同
///
/// 自连接发生在客户端绑定某端口后又连接同一端口，内核可能直接让连接"成功"。
/// 检测方法：比较 getsockname 和 getpeername 的 IP 和 port。
///
bool isSelfConnect(SocketHandle sockfd)
{
    struct sockaddr_in6 localaddr = getLocalAddr(sockfd);
    struct sockaddr_in6 peeraddr = getPeerAddr(sockfd);

    if (localaddr.sin6_family == AF_INET)
    {
        const auto* laddr4 = reinterpret_cast<struct sockaddr_in*>(&localaddr);
        const auto* raddr4 = reinterpret_cast<struct sockaddr_in*>(&peeraddr);
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
