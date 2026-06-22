#include "chaoxi/net/InetAddress.hpp"

#include "chaoxi/net/Endian.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <string_view>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace chaoxi::net
{
static_assert(sizeof(InetAddress) == sizeof(sockaddr_in6));
static_assert(offsetof(sockaddr_in, sin_family) == 0);
static_assert(offsetof(sockaddr_in6, sin6_family) == 0);

static constexpr in_addr_t kInaddrAny = INADDR_ANY;
static constexpr in_addr_t kInaddrLoopback = INADDR_LOOPBACK;

InetAddress::InetAddress(std::uint16_t portArg, bool loopbackOnly, bool ipv6)
{
    static_assert(offsetof(InetAddress, addr6_) == 0, "addr6_ offset 0");
    static_assert(offsetof(InetAddress, addr_) == 0, "addr_ offset 0");
    if (ipv6)
    {
        std::memset(&addr6_, 0, sizeof(addr6_));
        addr6_.sin6_family = AF_INET6;
        in6_addr ip = loopbackOnly ? in6addr_loopback : in6addr_any;
        addr6_.sin6_addr = ip;
        addr_.sin_port = sockets::hton16(portArg);
    }
    else
    {
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        in_addr_t ip = loopbackOnly ? kInaddrLoopback : kInaddrAny;
        addr_.sin_addr.s_addr = sockets::hton32(ip);
        addr_.sin_port = sockets::hton16(portArg);
    }
}

InetAddress::InetAddress(std::string_view ip, std::uint16_t port, bool ipv6)
{
    std::string ip_str{ip};
    if (ipv6 || ip_str.find(':') != std::string::npos)
    {
        std::memset(&addr6_, 0, sizeof(addr6_));
        sockets::fromIpPort(ip_str.data(), port, &addr6_);
    }
    else
    {
        std::memset(&addr_, 0, sizeof(addr_));
        sockets::fromIpPort(ip_str.data(), port, &addr_);
    }
}

std::string InetAddress::toIp() const noexcept
{
    char buf[INET6_ADDRSTRLEN] = {};
    sockets::toIp(buf, sizeof buf, getSockAddr());
    return buf;
}

std::string InetAddress::toIpPort() const noexcept
{
    char buf[64] = "";
    sockets::toIpPort(buf, sizeof buf, getSockAddr());
    return buf;
}

uint16_t InetAddress::port() const noexcept
{
    return sockets::ntoh16(portNetEndian());
}

uint32_t InetAddress::ipv4NetEndian() const noexcept
{
    assert(family() == AF_INET);
    return addr_.sin_addr.s_addr;
}

std::optional<InetAddress> InetAddress::resolve(std::string_view hostname)
{
    // 保证 \0 结尾
    std::string host_str{hostname};

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // 既接受 IPv4 也接受 IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP

    addrinfo* result = nullptr;

    // getaddrinfo 原生线程安全，且不需要调用者分配巨大内存
    if (::getaddrinfo(host_str.c_str(), nullptr, &hints, &result) != 0)
    {
        return std::nullopt;  // 解析失败直接返回空，异常流极其干净
    }

    std::optional<InetAddress> out;

    // 提取链表中的第一个结果
    if (result != nullptr)
    {
        if (result->ai_family == AF_INET)
        {
            out = InetAddress(*reinterpret_cast<sockaddr_in*>(result->ai_addr));
        }
        else if (result->ai_family == AF_INET6)
        {
            out = InetAddress(*reinterpret_cast<sockaddr_in6*>(result->ai_addr));
        }
        ::freeaddrinfo(result);  // 记得释放内部动态分配的内存
    }

    return out;
}

void InetAddress::setScopeId(uint32_t scope_id) noexcept
{
    if (family() == AF_INET6)
    {
        addr6_.sin6_scope_id = scope_id;
    }
}

}  // namespace chaoxi::net
