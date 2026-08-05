#pragma once

#include "chaoxi/net/Platform.hpp"

///
/// @file SocketOps.hpp
/// @brief 裸 socket 系统调用的 C++ 包装 —— 最底层的 socket 操作函数
///
/// 本文件属于命名空间 chaoxi::net::sockets，提供对 Linux socket 系统调用的
/// 直接包装，包括:
///   - 创建/关闭 socket fd
///   - 读写、bind、listen、accept、connect
///   - IP 地址与字符串互相转换
///   - socket 错误查询
///   - sockaddr 类型安全转换
///
/// 设计原则:
///   - 失败即终止（函数名带 OrDie 后缀） → LOG_FATAL + abort
///   - 错误返回码通过 errno 传递（保持 POSIX 语义）
///   - 全部为自由函数（namespace），不封装对象
///   - accept 等操作返回的 fd 自动设为非阻塞 + close-on-exec
///
/// 本层之上的 Socket 类提供 RAII 封装（析构自动 close），
/// 再之上是 TcpConnection 提供完整连接生命周期管理。
///

#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

namespace chaoxi::net::sockets
{

///
/// @brief 创建一个非阻塞 + close-on-exec 的 TCP socket
///
/// 内部使用 SOCK_NONBLOCK | SOCK_CLOEXEC 标志（Linux 特有），
/// 避免额外的 fcntl 调用。在 VALGRIND 模式下退化为传统方式。
///
/// @param family AF_INET 或 AF_INET6
/// @return sockfd，失败会 LOG_FATAL + abort
///
[[nodiscard]] SocketHandle createNonblockingOrDie(sa_family_t family);

/// @brief 创建非阻塞 TCP socket，失败返回 kInvalidSocket 并设置 errno。
[[nodiscard]] SocketHandle createNonblocking(sa_family_t family) noexcept;

/// @brief 将 socket 设为非阻塞；POSIX 下同时设置 close-on-exec。
int setNonblocking(SocketHandle sockfd) noexcept;

/// @brief 封装 ::read
[[nodiscard]] SignedSize read(SocketHandle sockfd, void* buf, size_t count);

/// @brief 封装 ::write
[[nodiscard]] SignedSize write(SocketHandle sockfd, const void* buf, size_t count);

/// @brief 封装 ::connect（非阻塞 connect 返回 -1，errno=EINPROGRESS）
int connect(SocketHandle sockfd, const struct sockaddr* addr);

/// @brief 非致命 bind/listen 包装，失败返回 -1 并设置 errno。
int bind(SocketHandle sockfd, const struct sockaddr* addr) noexcept;
int listen(SocketHandle sockfd) noexcept;

/// @brief 设置整数 socket option，失败返回 -1 并设置 errno。
int setSocketOption(SocketHandle sockfd,
                    int level,
                    int option,
                    int value) noexcept;

/// @brief 优雅关闭 socket 写端，失败返回 -1 并设置 errno。
int shutdownWrite(SocketHandle sockfd) noexcept;

/// @brief 开始监听，backlog 使用 SOMAXCONN，失败 fatal
void listenOrDie(SocketHandle sockfd);

/// @brief bind 到指定地址，失败 fatal
void bindOrDie(SocketHandle sockfd, const struct sockaddr* addr);

/// @brief 将 IP:port 字符串解析为 sockaddr_in（IPv4 版本）
void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr);

/// @brief 将 IP:port 字符串解析为 sockaddr_in6（IPv6 版本）
void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in6* addr);

/// @brief 将 sockaddr 格式化为 "IP:port" 或 "[IPv6]:port" 字符串（返回
/// std::string）
[[nodiscard]] std::string toIpPort(const struct sockaddr* addr);

/// @brief 将 sockaddr 格式化为 "IP:port" 字符串（写入用户提供缓冲区）
void toIpPort(char* buf, size_t size, const struct sockaddr* addr);

/// @brief 从 sockaddr 提取 IP 地址字符串（返回 std::string）
[[nodiscard]] std::string toIp(const struct sockaddr* addr);

/// @brief 从 sockaddr 提取 IP 地址字符串（写入用户提供缓冲区）
void toIp(char* buf, size_t size, const struct sockaddr* addr);

/// @brief 获取 socket 的 pending 错误（通过 SO_ERROR）
[[nodiscard]] int getSocketError(SocketHandle sockfd);

// ---- sockaddr 类型安全转换 ----
// 避免裸的 reinterpret_cast，将类型转换集中到一处
const struct sockaddr* sockaddr_cast(const struct sockaddr_in* addr);
const struct sockaddr* sockaddr_cast(const struct sockaddr_in6* addr);
struct sockaddr* sockaddr_cast(struct sockaddr_in6* addr);
const struct sockaddr_in* sockaddr_in_cast(const struct sockaddr* addr);
const struct sockaddr_in6* sockaddr_in6_cast(const struct sockaddr* addr);

/// @brief 非阻塞 accept，返回的 fd 自动设置为 SOCK_NONBLOCK | SOCK_CLOEXEC
/// @return 成功返回 >0 的 fd，失败返回 -1（errno 保留），错误会记录日志
[[nodiscard]] SocketHandle accept(SocketHandle sockfd, struct sockaddr_in6* addr);

/// @brief 关闭 fd（封装 ::close，失败记录日志）
void close(SocketHandle sockfd);

/// @brief 获取 socket 的本地地址
struct sockaddr_in6 getLocalAddr(SocketHandle sockfd);

/// @brief 获取 socket 的对端地址
struct sockaddr_in6 getPeerAddr(SocketHandle sockfd);

/// @brief 检测是否"自连接" — local 与 peer 地址相同
[[nodiscard]] bool isSelfConnect(SocketHandle sockfd);

}  // namespace chaoxi::net::sockets
