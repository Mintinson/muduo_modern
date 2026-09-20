#pragma once

#include "chaoxi/coro/AsyncSocket.hpp"
#include "chaoxi/coro/Task.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <exception>
#include <functional>
#include <memory>

namespace chaoxi::net
{
class EventLoop;
}  // namespace chaoxi::net

namespace chaoxi::coro
{

/**
 * @brief 基于协程的 TCP 服务器。
 *
 * 组合 AsyncAcceptor 和用户提供的连接处理器：
 *   - run() 协程循环 accept 新连接；
 *   - 每个连接通过 spawn 派生一个独立协程处理；
 *   - 支持从任意线程调用 stop() 优雅停止。
 *
 * 生命周期：
 *   - 构造时创建监听 socket，失败抛 std::system_error。
 *   - 析构时自动 stop()。
 *   - move-only。
 *
 * 线程模型：
 *   - 构造、run()、setErrorHandler() 必须在 loop 线程调用。
 *   - stop()、running() 线程安全。
 *   - localAddress() 可在任意线程调用（读取原子 fd）。
 */
class TcpServer
{
public:
    /**
     * @brief 连接处理器类型。
     *
     * 接收一个已接受的 AsyncSocket 和对端地址，返回 Task<void>。
     * 该 Task 会被 spawn 派生成独立协程执行。
     *
     * 使用 std::move_only_function 而非 std::function，因为处理器
     * 可能捕获 move-only 资源（例如 unique_ptr、promise 等）。
     */
    using ConnectionHandler =
        std::move_only_function<Task<void>(AsyncSocket, net::InetAddress)>;

    /**
     * @brief 错误处理器类型。
     *
     * 接收一个 std::exception_ptr，通常来自连接处理协程中未捕获的异常。
     * 使用 std::function 是因为它需要被拷贝到 spawn 的 lambda 中。
     */
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    /**
     * @brief 构造 TcpServer。
     * @param loop               目标 EventLoop。
     * @param listenAddress      监听地址。
     * @param connectionHandler  连接处理器，不能为空。
     * @param reusePort          是否启用 SO_REUSEPORT。
     *
     * 必须在 loop 线程调用。若 connectionHandler 为空抛 std::invalid_argument。
     * 监听 socket 创建失败抛 std::system_error。
     */
    TcpServer(net::EventLoop& loop,
              const net::InetAddress& listenAddress,
              ConnectionHandler connectionHandler,
              bool reusePort = false);

    /// 析构：调用 stop()，确保 run() 协程退出。
    ~TcpServer();

    TcpServer(TcpServer&& other) noexcept;
    TcpServer& operator=(TcpServer&& other) noexcept;

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    /**
     * @brief 返回本地监听地址。
     * @throws std::logic_error 若 state_ 为空。
     * @throws std::system_error 若已关闭。
     */
    [[nodiscard]] net::InetAddress localAddress() const;

    /// 是否正在运行（run() 协程处于 accept 循环中）。线程安全。
    [[nodiscard]] bool running() const noexcept;

    /**
     * @brief 设置错误处理器。
     *
     * 必须在 loop 线程调用。处理器用于接收连接处理协程中的异常。
     * 若未设置，spawn 的默认行为是 std::terminate()。
     */
    void setErrorHandler(ErrorHandler errorHandler);

    /**
     * @brief 启动服务器，进入 accept 循环。
     * @return Task<void> 惰性任务，需要被 co_await 或 spawn。
     *
     * 调用者应确保这个 Task 被执行（例如 spawn 到 loop 上），
     * 否则服务器不会开始接受连接。
     */
    Task<void> run();

    /**
     * @brief 请求停止服务器。
     *
     * 线程安全：
     *   - 设置 stopping 标志；
     *   - 关闭 acceptor，取消挂起的 accept；
     *   - run() 协程收到 operation_canceled 后正常退出。
     *
     * 注意：stop() 不等待 run() 协程实际退出，
     * 也不等待已派生的连接处理协程完成。
     */
    void stop();

private:
    struct State;  // 前向声明，定义在 .cpp 中。

    /**
     * @brief run() 的实现体。
     *
     * 与公开接口分离：run() 本身不含 co_await，返回惰性 Task；
     * runImpl 才是真正的协程体。
     */
    static Task<void> runImpl(std::shared_ptr<State> state);

    std::shared_ptr<State>
        state_;  ///< 共享状态，可能被 run 协程和错误处理器持有。
};

}  // namespace chaoxi::coro