#pragma once

#include "chaoxi/coro/AsyncFd.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <cstddef>
#include <span>

namespace chaoxi::coro
{

/**
 * @brief 异步 TCP socket 封装。
 *
 * AsyncSocket 在 AsyncFd 基础上增加 TCP 特有的操作：
 *   - 异步 connect（非阻塞 connect + 等待可写 + 检查 SO_ERROR）；
 *   - shutdownWrite（半关闭，发送 FIN）；
 *   - setTcpNoDelay（TCP_NODELAY，禁用 Nagle 算法）；
 *   - setKeepAlive（SO_KEEPALIVE，启用 TCP keepalive）。
 *
 * 设计要点：
 *   - 组合而非继承 AsyncFd。AsyncFd 提供通用的读/写/等待能力，
 *     AsyncSocket 在其上叠加 TCP 语义。
 *   - 所有 AsyncFd 的公开协程接口通过内联转发暴露，零开销。
 *   - move-only：底层 fd 和 Channel 都是独占资源，不能拷贝。
 *   - 必须在 loop 线程使用（除了 close 是线程安全的）。
 *
 * 生命周期：
 *   - 析构时自动调用 fd_.close()，若 ownership 为 owned 则关闭 fd。
 *   - 若 socket 由 connect() 创建，fd 归 AsyncSocket 所有。
 *   - 若由用户从已有 fd 构造，则默认 owned（见 AsyncFd 构造函数默认值），
 *     如需借用请使用 AsyncFd 的 borrowed 语义构造 AsyncSocket。
 */
class AsyncSocket
{
public:
    /**
     * @brief 从已有 fd 构造 AsyncSocket。
     * @param loop 目标 EventLoop。
     * @param fd   已创建的 socket fd，所有权转移给 AsyncSocket。
     *
     * 必须在 loop 线程调用。
     */
    AsyncSocket(net::EventLoop& loop, net::SocketHandle fd);

    AsyncSocket(AsyncSocket&&) noexcept = default;
    AsyncSocket& operator=(AsyncSocket&&) noexcept = default;

    AsyncSocket(const AsyncSocket&) = delete;
    AsyncSocket& operator=(const AsyncSocket&) = delete;
    ~AsyncSocket();

    /**
     * @brief 异步连接到指定地址。
     * @param loop    目标 EventLoop。
     * @param address 目标地址。
     * @return Task<AsyncSocket> 惰性任务，co_await 后返回已连接的 socket。
     *
     * 流程：
     *   1. 创建非阻塞 socket；
     *   2. 调用 connect：
     *      - 立即成功：直接返回。
     *      - 返回 EINPROGRESS/EINTR：等待可写事件，然后通过 SO_ERROR 检查结果；
     *   3. 若连接失败，抛出 std::system_error。
     *
     * 必须在 loop 线程被 co_await（因为内部会调用 assertInLoopThread）。
     */
    static Task<AsyncSocket> connect(net::EventLoop& loop,
                                     const net::InetAddress& address);

    /// 返回底层 fd；已关闭时返回 kInvalidSocket。线程安全。
    [[nodiscard]] net::SocketHandle nativeHandle() const noexcept
    {
        return fd_.nativeHandle();
    }

    /// 是否处于打开状态。线程安全。
    [[nodiscard]] bool isOpen() const noexcept { return fd_.isOpen(); }

    /// 读取最多 buffer.size() 字节。语义同 AsyncFd::readSome。
    Task<std::size_t> readSome(std::span<std::byte> buffer)
    {
        return fd_.readSome(buffer);
    }

    /// 读取恰好 buffer.size() 字节。语义同 AsyncFd::readExactly。
    Task<void> readExactly(std::span<std::byte> buffer)
    {
        return fd_.readExactly(buffer);
    }

    /// 写入最多 buffer.size() 字节。语义同 AsyncFd::writeSome。
    Task<std::size_t> writeSome(std::span<const std::byte> buffer)
    {
        return fd_.writeSome(buffer);
    }

    /// 写入恰好 buffer.size() 字节。语义同 AsyncFd::writeAll。
    Task<void> writeAll(std::span<const std::byte> buffer)
    {
        return fd_.writeAll(buffer);
    }

    /**
     * @brief 半关闭：关闭写方向，发送 FIN，但仍可读。
     *
     * 若底层 shutdown(SHUT_WR) 失败且 errno 不是 ENOTCONN，抛 std::system_error。
     * ENOTCONN 表示对端已断开，视为幂等成功。
     *
     * 必须在 loop 线程调用（操作 fd）。
     */
    void shutdownWrite();

    /// 设置 TCP_NODELAY。禁用 Nagle 算法，适合低延迟场景。必须在 loop 线程调用。
    void setTcpNoDelay(bool enabled);

    /// 设置 SO_KEEPALIVE。启用 TCP keepalive 探测。必须在 loop 线程调用。
    void setKeepAlive(bool enabled);

    /// 关闭 socket。线程安全，见 AsyncFd::close。
    void close() { fd_.close(); }

private:
    /**
     * @brief connect 的实现体。
     *
     * 与公开接口 connect 分离的原因：
     *   - connect 返回 Task<AsyncSocket>，是惰性协程；
     *   - connectImpl 是真正包含 co_await 的协程函数；
     *   - 这样 connect 本身没有 co_await，是普通函数，返回惰性 Task。
     *
     * 参数用值传递 InetAddress（而非 const&），因为协程可能在参数临时对象
     * 销毁后仍然挂起，必须持有自己的副本。
     */
    static Task<AsyncSocket> connectImpl(net::EventLoop& loop,
                                         net::InetAddress address);

    AsyncFd fd_;  ///< 底层异步 fd，提供读/写/等待能力。
};

}  // namespace chaoxi::coro