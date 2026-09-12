#pragma once

#include "chaoxi/net/Platform.hpp"
#include "chaoxi/coro/Task.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace chaoxi::net
{
class EventLoop;
}  // namespace chaoxi::net

namespace chaoxi::coro
{

enum class FdOwnership
{
    borrowed,
    owned,
};

class AsyncFd
{
public:
    AsyncFd(net::EventLoop& loop,
            net::SocketHandle fd,
            FdOwnership ownership = FdOwnership::owned);
    ~AsyncFd();

    AsyncFd(AsyncFd&& other) noexcept;
    AsyncFd& operator=(AsyncFd&& other) noexcept;

    AsyncFd(const AsyncFd&) = delete;
    AsyncFd& operator=(const AsyncFd&) = delete;

    [[nodiscard]] net::SocketHandle nativeHandle() const noexcept;
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] net::EventLoop& eventLoop() const;

    Task<void> waitReadable();
    Task<void> waitWritable();

    Task<std::size_t> readSome(std::span<std::byte> buffer);
    Task<void> readExactly(std::span<std::byte> buffer);
    Task<std::size_t> writeSome(std::span<const std::byte> buffer);
    Task<void> writeAll(std::span<const std::byte> buffer);

    void close();

private:
    struct State;

    explicit AsyncFd(std::shared_ptr<State> state) noexcept;

    static Task<void> waitReadableImpl(std::shared_ptr<State> state);
    static Task<void> waitWritableImpl(std::shared_ptr<State> state);
    static Task<std::size_t> readSomeImpl(std::shared_ptr<State> state,
                                          std::span<std::byte> buffer);
    static Task<void> readExactlyImpl(std::shared_ptr<State> state,
                                      std::span<std::byte> buffer);
    static Task<std::size_t> writeSomeImpl(std::shared_ptr<State> state,
                                           std::span<const std::byte> buffer);
    static Task<void> writeAllImpl(std::shared_ptr<State> state,
                                   std::span<const std::byte> buffer);

    std::shared_ptr<State> state_;
};

}  // namespace chaoxi::coro
