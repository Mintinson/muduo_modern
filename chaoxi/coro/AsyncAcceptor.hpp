#pragma once

#include "chaoxi/coro/AsyncSocket.hpp"
#include "chaoxi/coro/Task.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <memory>

namespace chaoxi::coro
{

/**
 * @brief accept() 成功时返回的连接信息。
 *
 * - socket：已接受的连接，拥有 fd 所有权（FdOwnership::owned）。
 * - peerAddress：对端地址。
 *
 * 该结构可移动，不可拷贝（因为 AsyncSocket 是 move-only）。
 */
struct AcceptedConnection
{
    AsyncSocket socket;
    net::InetAddress peerAddress;
};

/**
 * @brief 异步 TCP 连接接受器。
 *
 * 内部创建监听 socket，注册到 EventLoop，通过 Channel 监听可读事件。
 * 提供 accept() 协程接口，返回 Task<AcceptedConnection>。
 *
 * 设计要点：
 *   - 同一时刻只允许一个等待者（waiter_），多余的 accept() 调用会返回
 *     operation_in_progress。
 *   - 已接受但尚未被等待者取走的连接放入 pending_ 队列。
 *   - 下一次 accept() 若发现 pending_ 非空，会立即返回队首连接，不挂起。
 *   - 所有 EventLoop 操作必须在 loop 线程；close() 线程安全。
 *
 * 生命周期：
 *   - 构造时创建并绑定监听 fd，失败抛 std::system_error。
 *   - 析构时调用 close()，关闭监听 fd 和所有 pending 连接。
 *   - move-only。
 */
class AsyncAcceptor
{
public:
    /**
     * @brief 构造 AsyncAcceptor，创建监听 socket。
     * @param loop          目标 EventLoop。
     * @param listenAddress 监听地址（IP + 端口）。
     * @param reusePort     是否设置 SO_REUSEPORT（默认 false）。
     *
     * 必须在 loop 线程调用。内部依次执行：
     *   1. createNonblocking(family)
     *   2. setsockopt(SO_REUSEADDR)
     *   3. 可选 setsockopt(SO_REUSEPORT)
     *   4. bind + listen
     * 任一步失败都会关闭 fd 并抛 std::system_error。
     */
    AsyncAcceptor(net::EventLoop& loop,
                  const net::InetAddress& listenAddress,
                  bool reusePort = false);

    /// 析构：调用 close()，释放监听 fd 和 pending 连接。
    ~AsyncAcceptor();

    AsyncAcceptor(AsyncAcceptor&& other) noexcept;
    AsyncAcceptor& operator=(AsyncAcceptor&& other) noexcept;

    AsyncAcceptor(const AsyncAcceptor&) = delete;
    AsyncAcceptor& operator=(const AsyncAcceptor&) = delete;

    /**
     * @brief 返回本地监听地址。
     * @return net::InetAddress 实际绑定的地址（含端口）。
     * @throws std::system_error 若已关闭或 state_ 为空。
     *
     * 线程安全（读取原子 fd_），但 getLocalAddr 是系统调用。
     */
    [[nodiscard]] net::InetAddress localAddress() const;

    /// 是否处于打开状态（未被 close）。线程安全。
    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief 异步接受一个连接。
     * @return Task<AcceptedConnection> 惰性任务，co_await 后返回连接。
     *
     * 行为：
     *   - 若 pending_ 非空，立即返回队首连接，不挂起。
     *   - 若已有等待者，await_suspend 返回 false 并抛 operation_in_progress。
     *   - 若已关闭，抛 bad_file_descriptor。
     *   - 否则挂起，等待可读事件触发 handleAccept。
     *
     * 必须在 loop 线程被 co_await。
     */
    Task<AcceptedConnection> accept();

    /**
     * @brief 关闭监听器并取消所有等待者。
     *
     * 线程安全：非 loop 线程调用时会通过 queueInLoop 派发到 loop 线程。
     * 关闭后：
     *   - 等待者被唤醒并收到 operation_canceled 错误。
     *   - pending_ 中所有连接被关闭并清空。
     *   - 监听 fd 被关闭，Channel 从 EventLoop 移除。
     */
    void close();

private:
    struct State;  // 前向声明，定义在 .cpp 中。

    /**
     * @brief accept() 的实现体。
     *
     * 与公开接口分离的原因同 AsyncSocket：accept() 本身不含 co_await，
     * 返回惰性 Task；acceptImpl 才是真正的协程体。
     */
    static Task<AcceptedConnection> acceptImpl(std::shared_ptr<State> state);

    std::shared_ptr<State>
        state_;  ///< 共享状态，可能被协程和 Channel 回调持有。
};

}  // namespace chaoxi::coro