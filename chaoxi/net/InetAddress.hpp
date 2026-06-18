#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <netinet/in.h>
#include <sys/socket.h>

namespace chaoxi::net {
class InetAddress {
public:
    // InetAddress() noexcept = default;
    explicit InetAddress(std::uint16_t port = 0,
                         bool loopbackOnly = false,
                         bool ipv6 = false);

    InetAddress(std::string_view ip, std::uint16_t port, bool ipv6 = false);

    explicit InetAddress(const struct sockaddr_in& addr) : addr_{addr} {}

    explicit InetAddress(const struct sockaddr_in6& addr6) : addr6_{addr6} {}

    
    InetAddress(const InetAddress&) = default;
    InetAddress& operator=(const InetAddress&) = default;
    InetAddress(InetAddress&&) noexcept = default;
    InetAddress& operator=(InetAddress&&) noexcept = default;
    ~InetAddress() = default;
    
    [[nodiscard]]  sa_family_t family() const noexcept { return addr_.sin_family; }
    [[nodiscard]] std::string toIp() const noexcept;
    [[nodiscard]] std::string toIpPort() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;

    [[nodiscard]] const sockaddr* getSockAddr() const noexcept {
        return reinterpret_cast<const sockaddr*>(&addr6_);
    }

    void setSockAddrInet6(const struct sockaddr_in6& addr6) { addr6_ = addr6; }

    [[nodiscard]] std::uint32_t ipv4NetEndian() const noexcept;

    [[nodiscard]] std::uint16_t portNetEndian() const noexcept { return addr_.sin_port; }

    // resolve hostname to IP address, not changing port or sin_family
    // return true on success.
    // thread safe
    [[nodiscard]] static std::optional<InetAddress> resolve(std::string_view hostname);

    // set IPv6 ScopeId
    void setScopeId(std::uint32_t scope_id) noexcept;

private:
    union {
        struct sockaddr_in addr_{};
        struct sockaddr_in6 addr6_;
    };
};
}  // namespace chaoxi::net
