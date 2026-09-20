#pragma once

#include "chaoxi/coro/Task.hpp"
#include "chaoxi/net/Platform.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace chaoxi::net
{
class EventLoop;
}  // namespace chaoxi::net

namespace chaoxi::coro
{
/**
 * @brief 文件描述符的所有权语义。
 *
 * - borrowed：AsyncFd 不负责关闭 fd，由外部管理其生命周期。
 * - owned   ：AsyncFd 在 close() 或析构时关闭 fd。
 */
enum class FdOwnership : std::uint8_t
{
    borrowed,
    owned,
};

/**
 * @brief 异步文件描述符封装。
 *
 * 将裸 fd 封装成可在协程中异步读写、等待可读/可写的对象。
 * 内部通过 net::Channel 把 fd 注册到 EventLoop 的 epoll/poll 中，
 * 并在事件触发时恢复等待中的协程。
 *
 * 关键设计：
 *   - 真正的状态放在 shared_ptr<State> 中，因为协程可能比 AsyncFd 对象存活更久。
 *   - AsyncFd 本身是 move-only，允许多个对象共享同一个 State（内部私有构造）。
 *   - 所有 EventLoop 操作都必须在 loop 线程执行；close() 可以从任意线程调用，
 *     内部会通过 queueInLoop 派发到 loop 线程。
 *
 * 线程模型：
 *   - 公开的协程接口（waitReadable / readSome 等）必须在 loop 线程被 co_await。
 *   - close() 是线程安全的。
 *   - 其他方法（nativeHandle / isOpen /
 * eventLoop）可在任意线程调用（原子读取）。
 */
class AsyncFd
{
public:
    /**
     * @brief 构造 AsyncFd。
     * @param loop      目标 EventLoop。
     * @param fd        文件描述符。
     * @param ownership 所有权语义，默认 owned。
     *
     * 必须在 loop 线程调用；内部会创建 Channel 并注册回调。
     * 若 fd 无效则抛出 std::system_error。
     */
    AsyncFd(net::EventLoop& loop,
            net::SocketHandle fd,
            FdOwnership ownership = FdOwnership::owned);

    /// 析构：调用 close()，确保 fd 和 Channel 被正确清理。
    ~AsyncFd();

    AsyncFd(AsyncFd&& other) noexcept;
    AsyncFd& operator=(AsyncFd&& other) noexcept;

    AsyncFd(const AsyncFd&) = delete;
    AsyncFd& operator=(const AsyncFd&) = delete;

    /// 返回底层 fd；若已关闭则返回 kInvalidSocket。线程安全。
    [[nodiscard]] net::SocketHandle nativeHandle() const noexcept;

    /// 是否处于打开状态（未被 close）。线程安全。
    [[nodiscard]] bool isOpen() const noexcept;

    /// 返回关联的 EventLoop；若 state_ 为空则抛 std::logic_error。
    [[nodiscard]] net::EventLoop& eventLoop() const;

    /// 挂起当前协程，直到 fd 可读。
    Task<void> waitReadable();

    /// 挂起当前协程，直到 fd 可写。
    Task<void> waitWritable();

    /// 读取最多 buffer.size() 字节，返回实际读取的字节数（可能为 0）。
    Task<std::size_t> readSome(std::span<std::byte> buffer);

    /// 读取恰好 buffer.size() 字节；若对端关闭则抛 connection_reset。
    Task<void> readExactly(std::span<std::byte> buffer);

    /// 写入最多 buffer.size() 字节，返回实际写入的字节数。
    Task<std::size_t> writeSome(std::span<const std::byte> buffer);

    /// 写入恰好 buffer.size() 字节；若写入返回 0 则抛 io_error。
    Task<void> writeAll(std::span<const std::byte> buffer);

    /**
     * @brief 关闭 fd 并取消所有等待者。
     *
     * 线程安全：如果不在 loop 线程，会通过 queueInLoop 派发到 loop 线程执行。
     * 关闭后：
     *   - 所有挂起的等待者被唤醒并收到 operation_canceled 错误。
     *   - Channel 从 EventLoop 中移除。
     *   - 若 ownership 为 owned，关闭 fd。
     */
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

    // 共享状态，可能被多个 AsyncFd 或协程持有。
    std::shared_ptr<State> state_;
};

}  // namespace chaoxi::coro
