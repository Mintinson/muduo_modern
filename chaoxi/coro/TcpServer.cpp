#include "chaoxi/coro/TcpServer.hpp"

#include "chaoxi/coro/AsyncAcceptor.hpp"
#include "chaoxi/coro/Spawn.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <atomic>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace chaoxi::coro
{

/**
 * @brief TcpServer 的共享状态。
 *
 * 为什么需要 shared_ptr<State>？
 *   - runImpl 协程持有 state 的 shared_ptr，保证执行期间 State 存活；
 *   - spawn 的错误处理器也持有 weak_ptr<State>，安全地检查服务器是否仍存活；
 *   - TcpServer 对象本身持有 shared_ptr，stop() 时释放。
 *
 * 线程模型：
 *   - loop、acceptor、connectionHandler、errorHandler 只在 loop 线程访问；
 *   - running、stopping 是原子变量，可在任意线程访问。
 */
struct TcpServer::State
{
    State(net::EventLoop& eventLoop,
          const net::InetAddress& listenAddress,
          ConnectionHandler handler,
          bool reusePort)
        : loop(eventLoop)
        , acceptor(eventLoop, listenAddress, reusePort)
        , connectionHandler(std::move(handler))
    {
        // 连接处理器不能为空：否则 accept 到连接后无法处理。
        if (!connectionHandler)
        {
            throw std::invalid_argument(
                "TcpServer requires a connection handler");
        }
    }

    net::EventLoop& loop;                 ///< 目标 EventLoop。
    AsyncAcceptor acceptor;               ///< 监听器，负责接受连接。
    ConnectionHandler connectionHandler;  ///< 用户提供的连接处理器。
    ErrorHandler errorHandler;            ///< 可选的错误处理器。
    std::atomic_bool running{false};      ///< run() 是否正在执行 accept 循环。
    std::atomic_bool stopping{false};     ///< 是否已请求停止。
};

/**
 * @brief 构造 TcpServer。
 *
 * 创建 State，内部会创建并绑定监听 socket。
 * 必须在 loop 线程调用（AsyncAcceptor 构造会 assertInLoopThread）。
 */
TcpServer::TcpServer(net::EventLoop& loop,
                     const net::InetAddress& listenAddress,
                     ConnectionHandler connectionHandler,
                     bool reusePort)
    : state_(std::make_shared<State>(
          loop, listenAddress, std::move(connectionHandler), reusePort))
{
}

/// 析构：调用 stop()，确保 run() 协程被唤醒并退出。
TcpServer::~TcpServer()
{
    stop();
}

TcpServer::TcpServer(TcpServer&& other) noexcept
    : state_(std::move(other.state_))
{
}

TcpServer& TcpServer::operator=(TcpServer&& other) noexcept
{
    if (this != &other)
    {
        stop();  // 先停止当前服务器。
        state_ = std::move(other.state_);
    }
    return *this;
}

/**
 * @brief 返回本地监听地址。
 * @throws std::logic_error 若 state_ 为空。
 */
net::InetAddress TcpServer::localAddress() const
{
    if (!state_)
    {
        throw std::logic_error("TcpServer has no state");
    }
    return state_->acceptor.localAddress();
}

/// 是否正在运行。线程安全（原子读取）。
bool TcpServer::running() const noexcept
{
    return state_ && state_->running.load(std::memory_order_acquire);
}

/**
 * @brief 设置错误处理器。
 *
 * 必须在 loop 线程调用，因为 errorHandler 只在 loop 线程被读取和使用。
 */
void TcpServer::setErrorHandler(ErrorHandler errorHandler)
{
    if (!state_)
    {
        throw std::logic_error("TcpServer has no state");
    }
    state_->loop.assertInLoopThread();
    state_->errorHandler = std::move(errorHandler);
}

/// 公开接口：委托给 runImpl，转移 state_ 的 shared_ptr 副本。
Task<void> TcpServer::run()
{
    return runImpl(state_);
}

/**
 * @brief run() 的实现体：accept 循环。
 *
 * 流程：
 *   1. 用 compare_exchange 保证只有一个 run() 实例。
 *   2. 用 RunningGuard 保证退出时 running 置回 false。
 *   3. 循环：
 *      - 检查 stopping，若已请求停止则退出。
 *      - co_await acceptor.accept() 等待新连接。
 *      - 调用 connectionHandler 得到处理 Task。
 *      - 用 spawn 派生独立协程处理连接，异常交给 errorHandler。
 *   4. 若 accept 抛出 operation_canceled 且 stopping 为 true，正常退出。
 *      其他异常重新抛出。
 *
 * 注意：
 *   - 每个连接处理都在独立协程中，accept 循环不被阻塞。
 *   - spawn 的错误处理器用 weak_ptr<State> 捕获，避免延长 State 生命周期。
 *   - 若 State 已销毁或用户未设置 errorHandler，异常将 std::terminate()。
 */
Task<void> TcpServer::runImpl(std::shared_ptr<State> state)
{
    if (!state)
    {
        throw std::logic_error("TcpServer has no state");
    }

    // 保证只有一个 run() 实例在运行。
    bool expected = false;
    if (!state->running.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel))
    {
        throw std::system_error(
            std::make_error_code(std::errc::operation_in_progress));
    }

    // RAII 守卫：无论正常返回还是异常退出，都把 running 置回 false。
    struct RunningGuard
    {
        std::atomic_bool& running;

        ~RunningGuard() { running.store(false, std::memory_order_release); }
    } guard{state->running};

    // accept 循环。
    while (!state->stopping.load(std::memory_order_acquire))
    {
        try
        {
            // 等待新连接。可能挂起，也可能立即返回（pending_ 非空时）。
            auto connection = co_await state->acceptor.accept();

            // 调用用户处理器，得到处理 Task。
            // 注意：必须把 socket 和 peerAddress 移入，因为 AsyncSocket 是
            // move-only。
            auto task = state->connectionHandler(
                std::move(connection.socket), std::move(connection.peerAddress));

            // 用 weak_ptr 捕获 State，避免 spawn 的错误处理器延长 State
            // 生命周期。
            std::weak_ptr<State> weakState = state;

            // 派生独立协程处理连接。
            // 异常会交给错误处理器；若 State 已销毁或未设置处理器，则
            // std::terminate。
            spawn(state->loop, std::move(task),
                  [weakState](std::exception_ptr error)
                  {
                      if (auto locked = weakState.lock();
                          locked && locked->errorHandler)
                      {
                          locked->errorHandler(error);
                          return;
                      }
                      std::terminate();
                  });
        }
        catch (const std::system_error& error)
        {
            // stop() 会关闭 acceptor，导致 accept 抛出 operation_canceled。
            // 若此时 stopping 为 true，视为正常退出，不传播异常。
            if (state->stopping.load(std::memory_order_acquire) &&
                error.code() ==
                    std::make_error_code(std::errc::operation_canceled))
            {
                co_return;
            }
            // 其他错误（例如监听 fd 出错）继续传播。
            throw;
        }
    }
}

/**
 * @brief 请求停止服务器。
 *
 * 线程安全：
 *   - 设置 stopping 标志，让 runImpl 循环在下一次检查时退出；
 *   - 调用 acceptor.close()，取消挂起的 accept，唤醒 runImpl 协程；
 *   - acceptor.close() 内部会根据线程决定直接执行还是 queueInLoop。
 *
 * 注意：
 *   - stop() 不等待 runImpl 协程实际退出；
 *   - stop() 也不等待已派生的连接处理协程完成（fire-and-forget）；
 *   - 已接受的连接由各自的 AsyncSocket 负责关闭。
 */
void TcpServer::stop()
{
    auto state = state_;
    if (!state)
    {
        return;
    }
    // 设置停止标志，runImpl 循环会在下一次检查时退出。
    state->stopping.store(true, std::memory_order_release);
    // 关闭 acceptor，取消挂起的 accept。
    state->acceptor.close();
}

}  // namespace chaoxi::coro