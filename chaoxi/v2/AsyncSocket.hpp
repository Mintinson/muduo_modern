#pragma once

#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/v2/AsyncFd.hpp"

#include <cstddef>
#include <span>

namespace chaoxi::v2
{

class AsyncSocket
{
public:
    AsyncSocket(net::EventLoop& loop, net::SocketHandle fd);

    AsyncSocket(AsyncSocket&&) noexcept = default;
    AsyncSocket& operator=(AsyncSocket&&) noexcept = default;

    AsyncSocket(const AsyncSocket&) = delete;
    AsyncSocket& operator=(const AsyncSocket&) = delete;

    static Task<AsyncSocket> connect(net::EventLoop& loop,
                                     const net::InetAddress& address);

    [[nodiscard]] net::SocketHandle nativeHandle() const noexcept
    {
        return fd_.nativeHandle();
    }

    [[nodiscard]] bool isOpen() const noexcept { return fd_.isOpen(); }

    Task<std::size_t> readSome(std::span<std::byte> buffer)
    {
        return fd_.readSome(buffer);
    }

    Task<void> readExactly(std::span<std::byte> buffer)
    {
        return fd_.readExactly(buffer);
    }

    Task<std::size_t> writeSome(std::span<const std::byte> buffer)
    {
        return fd_.writeSome(buffer);
    }

    Task<void> writeAll(std::span<const std::byte> buffer)
    {
        return fd_.writeAll(buffer);
    }

    void shutdownWrite();
    void setTcpNoDelay(bool enabled);
    void setKeepAlive(bool enabled);

    void close() { fd_.close(); }

private:
    static Task<AsyncSocket> connectImpl(net::EventLoop& loop,
                                         net::InetAddress address);

    AsyncFd fd_;
};

}  // namespace chaoxi::v2
