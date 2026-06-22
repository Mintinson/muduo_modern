///
/// @file SocketOps.cpp
/// @brief 裸 socket 系统调用的实现 —— 每个函数对应一个 POSIX 系统调用
///

#include "chaoxi/net/SocketOps.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Endian.hpp"

#include <cassert>
#include <cstring>
#include <format>

#include <sys/socket.h>
#include <sys/uio.h>  // readv
#include <unistd.h>

namespace chaoxi::net::sockets
{

/// @brief 封装 ::read
ssize_t read(int sockfd, void* buf, size_t count)
{
    return ::read(sockfd, buf, count);
}

/// @brief 封装 ::write
ssize_t write(int sockfd, const void* buf, size_t count)
{
    return ::write(sockfd, buf, count);
}

/// @brief 通过 getsockopt(SO_ERROR) 获取 socket 的 pending 错误
///
/// SO_ERROR 被 getsockopt 读取后会被内核自动清零。
/// 如果 getsockopt 本身出错（参数非法），返回 errno。
///
int getSocketError(int sockfd)
{
    int optval{};
    socklen_t optlen = static_cast<socklen_t>(sizeof optval);

    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        return errno;
    }
    else
    {
        return optval;
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
int createNonblockingOrDie(sa_family_t family)
{
#ifdef VALGRIND
    int sockfd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_SYSFATAL << "sockets::createNonblockingOrDie";
    }
    setNonBlockAndCloseOnExec(sockfd);
#else
    int sockfd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_SYSFATAL << "sockets::createNonblockingOrDie";
    }
#endif
    return sockfd;
}

/// @brief 非阻塞 connect
///
/// 当返回 -1 且 errno == EINPROGRESS 时是正常情况：
/// 内核正在后台进行 TCP 三次握手，后续通过 epoll 可写事件检测连接完成。
///
int connect(int sockfd, const struct sockaddr* addr)
{
    return ::connect(sockfd, addr, static_cast<socklen_t>(sizeof(*addr)));
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
        buf[0] = '[';
        toIp(buf + 1, size - 1, addr);
        size_t end = ::strlen(buf);
        const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
        uint16_t port = sockets::ntoh16(addr6->sin6_port);
        assert(size > end);
        std::format_to_n(buf + end, static_cast<int>(size - end), "]:{}", port);
        return;
    }
    toIp(buf, size, addr);
    size_t end = ::strlen(buf);
    const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
    uint16_t port = sockets::ntoh16(addr4->sin_port);
    assert(size > end);
    std::format_to_n(buf + end, static_cast<int>(size - end), ":{}", port);
}

/// @brief 从 sockaddr 提取 IP 地址字符串（缓冲区版本）
void toIp(char* buf, size_t size, const struct sockaddr* addr)
{
    if (addr->sa_family == AF_INET)
    {
        assert(size >= INET_ADDRSTRLEN);
        const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
        ::inet_ntop(AF_INET, &addr4->sin_addr, buf,
                    static_cast<socklen_t>(size));
    }
    else if (addr->sa_family == AF_INET6)
    {
        assert(size >= INET6_ADDRSTRLEN);
        const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
        ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf,
                    static_cast<socklen_t>(size));
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
void listenOrDie(int sockfd)
{
    int ret = ::listen(sockfd, SOMAXCONN);
    if (ret < 0)
    {
        LOG_SYSFATAL << "sockets::listenOrDie";
    }
}

/// @brief bind 到指定地址，失败 fatal
void bindOrDie(int sockfd, const struct sockaddr* addr)
{
    // 统一使用 sockaddr_in6 的大小以兼容 IPv4 和 IPv6
    int ret = ::bind(sockfd, addr,
                     static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
    if (ret < 0)
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
int accept(int sockfd, struct sockaddr_in6* addr)
{
    socklen_t addrlen = static_cast<socklen_t>(sizeof *addr);
#if defined (VALGRIND) || defined(NO_ACCEPT4)
    int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
    setNonBlockAndCloseOnExec(connfd);
#else
    int connfd = ::accept4(sockfd, sockaddr_cast(addr), &addrlen,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
#endif
    if (connfd < 0)
    {
        int savedErrno = errno;
        LOG_SYSERR << "Socket::accept";
        switch (savedErrno)
        {
            case EAGAIN:
            case ECONNABORTED:
            case EINTR:
            case EPROTO:
            case EPERM:
            case EMFILE:
                // 期望的错误，errno 保留给调用者
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
                LOG_FATAL << "unexpected error of ::accept " << savedErrno;
                break;
            default:
                LOG_FATAL << "unknown error of ::accept " << savedErrno;
                break;
        }
    }
    return connfd;
}

/// @brief 获取本地地址（通过 getsockname）
struct sockaddr_in6 getLocalAddr(int sockfd)
{
    struct sockaddr_in6 localaddr{};
    socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
    if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0)
    {
        LOG_SYSERR << "sockets::getLocalAddr";
    }
    return localaddr;
}

/// @brief 获取对端地址（通过 getpeername）
struct sockaddr_in6 getPeerAddr(int sockfd)
{
    struct sockaddr_in6 peeraddr{};
    socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
    if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0)
    {
        LOG_SYSERR << "sockets::getPeerAddr";
    }
    return peeraddr;
}

/// @brief 关闭 fd（封装 ::close，失败记录日志）
void close(int sockfd)
{
    if (::close(sockfd) < 0)
    {
        LOG_SYSERR << "sockets::close";
    }
}

/// @brief 检测是否"自连接" — local 与 peer 地址完全相同
///
/// 自连接发生在客户端绑定某端口后又连接同一端口，内核可能直接让连接"成功"。
/// 检测方法：比较 getsockname 和 getpeername 的 IP 和 port。
///
bool isSelfConnect(int sockfd)
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
