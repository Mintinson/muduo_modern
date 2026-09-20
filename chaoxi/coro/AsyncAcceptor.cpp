#include "chaoxi/coro/AsyncAcceptor.hpp"

#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <atomic>
#include <cerrno>
#include <coroutine>
#include <deque>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

namespace chaoxi::coro
{

/**
 * @brief AsyncAcceptor 的共享状态。
 *
 * 职责：
 *   - 持有监听 fd、EventLoop、Channel。
 *   - 管理唯一的等待者（waiter_）和已接受但未交付的连接队列（pending_）。
 *   - 处理可读事件，循环 accept。
 *   - 提供线程安全的 close。
 *
 * 线程模型：
 *   - 除 closed_ / fd_ 外，所有成员只在 loop 线程访问。
 *   - closed_ 和 fd_ 是原子变量，可在任意线程读取。
 */
struct AsyncAcceptor::State : std::enable_shared_from_this<State>
{
    /**
     * @brief 已接受但尚未交付给等待者的连接。
     */
    struct PendingConnection
    {
        net::SocketHandle fd;          ///< 已接受的连接 fd。
        net::InetAddress peerAddress;  ///< 对端地址。
    };

    /**
     * @brief 等待者信息。
     *
     * 存储在 Awaiter 内部，由 State::waiter_ 以指针持有。
     * 协程句柄在等待期间有效；完成时填入 error 或 connection。
     */
    struct Waiter
    {
        std::coroutine_handle<> coroutine{};          ///< 等待中的协程句柄。
        std::error_code error;                        ///< 完成时的错误码。
        std::optional<PendingConnection> connection;  ///< 成功时的连接。
    };

    /**
     * @brief 可被 co_await 的等待器。
     *
     * await 流程：
     *   1. await_ready 返回 false，总是尝试挂起。
     *   2. await_suspend：
     *      - 若已关闭：返回 false，await_resume 抛 bad_file_descriptor。
     *      - 若 pending_ 非空：取出队首连接，返回 false，不挂起。
     *      - 若已有等待者：返回 false，await_resume 抛 operation_in_progress。
     *      - 否则注册自己到 State::waiter_，返回 true 挂起。
     *   3. await_resume：若有 error 则抛；否则构造 AcceptedConnection 返回。
     *
     * Awaiter 不可移动/拷贝，因为 State 以指针持有其内部 Waiter 的地址。
     */
    class Awaiter
    {
    public:
        explicit Awaiter(std::shared_ptr<State> state) noexcept
            : state_(std::move(state))
        {
        }

        /// 析构：若仍在等待，从 State 中注销自己，避免悬垂指针。
        ~Awaiter()
        {
            if (waiter_.coroutine && state_->waiter_ == &waiter_)
            {
                state_->waiter_ = nullptr;
            }
        }

        // 禁止移动和拷贝：地址被 State 持有，移动会导致悬垂。
        Awaiter(Awaiter&&) noexcept = delete;
        Awaiter(const Awaiter&) = delete;
        Awaiter& operator=(Awaiter&&) noexcept = delete;
        Awaiter& operator=(const Awaiter&) = delete;

        /// 总是返回 false，由 await_suspend 决定是否真正挂起。
        [[nodiscard]] bool await_ready() const noexcept { return false; }

        /**
         * @brief 挂起当前协程或立即完成。
         * @param coroutine 当前协程句柄。
         * @return true  表示成功挂起，等待 handleAccept 唤醒。
         *         false 表示不挂起，await_resume
         * 会立刻执行并可能抛错或返回连接。
         *
         * 必须在 loop 线程调用。
         */
        bool await_suspend(std::coroutine_handle<> coroutine)
        {
            state_->loop_.assertInLoopThread();
            waiter_.coroutine = coroutine;

            // 1. 已关闭：直接失败。
            if (state_->closed_.load(std::memory_order_acquire))
            {
                waiter_.coroutine = {};
                waiter_.error =
                    std::make_error_code(std::errc::bad_file_descriptor);
                return false;
            }

            // 2. 已有 pending 连接：立即取一个，不挂起。
            if (!state_->pending_.empty())
            {
                waiter_.connection = state_->pending_.front();
                state_->pending_.pop_front();
                waiter_.coroutine = {};
                return false;
            }

            // 3. 已有等待者：不允许并发等待。
            if (state_->waiter_ != nullptr)
            {
                waiter_.coroutine = {};
                waiter_.error =
                    std::make_error_code(std::errc::operation_in_progress);
                return false;
            }

            // 4. 注册自己，挂起等待可读事件。
            state_->waiter_ = &waiter_;
            return true;
        }

        /**
         * @brief 恢复后执行：返回连接或抛出错误。
         * @return AcceptedConnection 包含已接受的 socket 和对端地址。
         */
        AcceptedConnection await_resume()
        {
            if (waiter_.error)
            {
                throw std::system_error(waiter_.error);
            }
            if (!waiter_.connection)
            {
                throw std::system_error(
                    std::make_error_code(std::errc::io_error));
            }

            auto connection = std::move(*waiter_.connection);
            // 用已接受的 fd 构造 AsyncSocket，所有权转移给它。
            return AcceptedConnection{
                .socket = AsyncSocket{state_->loop_, connection.fd},
                .peerAddress = connection.peerAddress
            };
        }

    private:
        std::shared_ptr<State> state_;  ///< 保证 State 在等待期间存活。
        Waiter waiter_;                 ///< 等待者信息，地址被 State 持有。
    };

    /**
     * @brief 构造 State：创建、配置、绑定、监听 fd。
     * @param loop          目标 EventLoop。
     * @param listenAddress 监听地址。
     * @param reusePort     是否启用 SO_REUSEPORT。
     *
     * 必须在 loop 线程调用。任一步失败都会关闭 fd 并抛 std::system_error。
     */
    State(net::EventLoop& loop,
          const net::InetAddress& listenAddress,
          bool reusePort)
        : loop_(loop)
        , fd_(net::sockets::createNonblocking(listenAddress.family()))
        , channel_(&loop, fd_.load(std::memory_order_relaxed))
    {
        loop_.assertInLoopThread();
        const net::SocketHandle fd = fd_.load(std::memory_order_relaxed);
        if (fd == net::kInvalidSocket)
        {
            throw std::system_error(errno, std::generic_category());
        }

        const int enabled = 1;

        // 设置 SO_REUSEADDR，避免 TIME_WAIT 导致 bind 失败。
        if (net::sockets::setSocketOption(fd, SOL_SOCKET, SO_REUSEADDR,
                                          enabled) < 0)
        {
            const int error = errno;
            net::sockets::close(fd);
            fd_.store(net::kInvalidSocket, std::memory_order_relaxed);
            throw std::system_error(error, std::generic_category());
        }

#ifdef SO_REUSEPORT
        // 可选 SO_REUSEPORT：允许多个进程/线程绑定同一端口，内核负载均衡。
        if (reusePort && net::sockets::setSocketOption(
                             fd, SOL_SOCKET, SO_REUSEPORT, enabled) < 0)
        {
            const int error = errno;
            net::sockets::close(fd);
            fd_.store(net::kInvalidSocket, std::memory_order_relaxed);
            throw std::system_error(error, std::generic_category());
        }
#else
        // 平台不支持 SO_REUSEPORT 时，若请求启用则报错。
        if (reusePort)
        {
            net::sockets::close(fd);
            fd_.store(net::kInvalidSocket, std::memory_order_relaxed);
            throw std::system_error(
                std::make_error_code(std::errc::operation_not_supported));
        }
#endif

        // 绑定并监听。
        if (net::sockets::bind(fd, listenAddress.getSockAddr()) < 0 ||
            net::sockets::listen(fd) < 0)
        {
            const int error = errno;
            net::sockets::close(fd);
            fd_.store(net::kInvalidSocket, std::memory_order_relaxed);
            throw std::system_error(error, std::generic_category());
        }
    }

    /**
     * @brief 工厂方法：创建 State 并初始化 Channel 回调。
     *
     * 分两步是因为 shared_from_this 要求对象已由 shared_ptr 管理，
     * 不能在构造函数中调用。
     */
    static std::shared_ptr<State> create(net::EventLoop& loop,
                                         const net::InetAddress& listenAddress,
                                         bool reusePort)
    {
        auto state = std::make_shared<State>(loop, listenAddress, reusePort);
        state->initialize();
        return state;
    }

    /**
     * @brief 初始化 Channel 回调并注册可读事件。
     *
     * 使用 weak_ptr 避免 State 与 Channel 循环引用。
     */
    void initialize()
    {
        std::weak_ptr<State> weakState = shared_from_this();
        channel_.tie(shared_from_this());

        // 可读事件：有连接到来。
        channel_.setReadCallback(
            [weakState](Timestamp)
            {
                if (auto state = weakState.lock())
                {
                    state->handleAccept();
                }
            });

        // 错误事件：监听 fd 出错，通知等待者。
        channel_.setErrorCallback(
            [weakState]
            {
                if (auto state = weakState.lock())
                {
                    int error = net::sockets::getSocketError(
                        state->fd_.load(std::memory_order_acquire));
                    if (error == 0)
                    {
                        error = EIO;
                    }
                    state->completeError(
                        std::error_code(error, std::generic_category()));
                }
            });

        // 注册可读事件到 EventLoop。
        channel_.enableReading();
        registered_ = true;
    }

    /**
     * @brief 处理可读事件：循环 accept 直到 EAGAIN。
     *
     * 必须在 loop 线程调用。
     * - 成功 accept：交付连接（deliver）。
     * - EINTR / ECONNABORTED：重试。
     * - EAGAIN / EWOULDBLOCK：没有更多连接，返回。
     * - 其他错误：completeError 通知等待者，返回。
     */
    void handleAccept()
    {
        loop_.assertInLoopThread();
        while (true)
        {
            sockaddr_in6 peer{};
            const net::SocketHandle connectionFd =
                net::sockets::accept(fd_.load(std::memory_order_acquire), &peer);
            if (connectionFd != net::kInvalidSocket)
            {
                // 成功接受一个连接，交付。
                deliver(PendingConnection{
                    .fd = connectionFd, .peerAddress = net::InetAddress{peer}});
                continue;
            }
            if (errno == EINTR || errno == ECONNABORTED)
            {
                // 被信号中断或连接在 accept 前被对端放弃，重试。
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 没有更多待处理连接。
                return;
            }
            // 严重错误：通知等待者。
            completeError(std::error_code(errno, std::generic_category()));
            return;
        }
    }

    /**
     * @brief 交付一个已接受的连接。
     * @param connection 已接受的连接。
     *
     * 若有等待者：填入 waiter_->connection，投递恢复任务。
     * 若无等待者：放入 pending_ 队列，等待下一次 accept() 取走。
     *
     * 必须在 loop 线程调用。
     */
    void deliver(PendingConnection connection)
    {
        if (waiter_ == nullptr)
        {
            // 无等待者，缓存起来。
            pending_.push_back(std::move(connection));
            return;
        }

        // 有等待者：取出 waiter_，填入连接，恢复协程。
        Waiter* waiter = std::exchange(waiter_, nullptr);
        waiter->connection = std::move(connection);
        auto coroutine = std::exchange(waiter->coroutine, {});
        loop_.queueInLoop(
            [coroutine]
            {
                // 防御性检查：句柄有效且未完成。
                if (coroutine && !coroutine.done())
                {
                    coroutine.resume();
                }
            });
    }

    /**
     * @brief 向等待者传递错误。
     * @param error 错误码。
     *
     * 若无等待者则忽略（错误只对当前等待者有意义）。
     * 必须在 loop 线程调用。
     */
    void completeError(std::error_code error)
    {
        if (waiter_ == nullptr)
        {
            return;
        }

        Waiter* waiter = std::exchange(waiter_, nullptr);
        waiter->error = error;
        auto coroutine = std::exchange(waiter->coroutine, {});
        loop_.queueInLoop(
            [coroutine]
            {
                if (coroutine && !coroutine.done())
                {
                    coroutine.resume();
                }
            });
    }

    /**
     * @brief 在 loop 线程中关闭监听器。
     *
     * 步骤：
     *   1. 原子设置 closed_，若已关闭则返回。
     *   2. 以 operation_canceled 通知等待者。
     *   3. 关闭 pending_ 中所有连接并清空。
     *   4. 从 EventLoop 移除 Channel。
     *   5. 关闭监听 fd。
     *
     * 必须在 loop 线程调用。
     */
    void closeInLoop()
    {
        loop_.assertInLoopThread();
        if (closed_.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        // 取消等待者。
        completeError(std::make_error_code(std::errc::operation_canceled));

        // 关闭所有已接受但未交付的连接。
        for (auto& connection : pending_)
        {
            net::sockets::close(connection.fd);
        }
        pending_.clear();

        // 从 EventLoop 移除 Channel。
        if (registered_)
        {
            channel_.disableAll();
            channel_.remove();
            registered_ = false;
        }

        // 关闭监听 fd。
        const net::SocketHandle fd =
            fd_.exchange(net::kInvalidSocket, std::memory_order_acq_rel);
        if (fd != net::kInvalidSocket)
        {
            net::sockets::close(fd);
        }
    }

    net::EventLoop& loop_;                   ///< 目标 EventLoop。
    std::atomic<net::SocketHandle> fd_;      ///< 监听 fd，可跨线程读取。
    net::Channel channel_;                   ///< 用于注册可读事件。
    std::atomic_bool closed_{false};         ///< 是否已关闭，可跨线程读取。
    bool registered_ = false;                ///< Channel 是否已注册。
    std::deque<PendingConnection> pending_;  ///< 已接受但未交付的连接队列。
    Waiter* waiter_ = nullptr;               ///< 当前等待者，仅 loop 线程访问。
};

/**
 * @brief 公开构造函数：创建 State。
 *
 * 必须在 loop 线程调用。
 */
AsyncAcceptor::AsyncAcceptor(net::EventLoop& loop,
                             const net::InetAddress& listenAddress,
                             bool reusePort)
    : state_(State::create(loop, listenAddress, reusePort))
{
}

/// 析构：调用 close()，释放监听 fd 和 pending 连接。
AsyncAcceptor::~AsyncAcceptor()
{
    close();
}

AsyncAcceptor::AsyncAcceptor(AsyncAcceptor&& other) noexcept
    : state_(std::move(other.state_))
{
}

AsyncAcceptor& AsyncAcceptor::operator=(AsyncAcceptor&& other) noexcept
{
    if (this != &other)
    {
        close();  // 先释放当前资源。
        state_ = std::move(other.state_);
    }
    return *this;
}

/**
 * @brief 返回本地监听地址。
 * @throws std::system_error 若已关闭或 state_ 为空。
 */
net::InetAddress AsyncAcceptor::localAddress() const
{
    if (!state_ || state_->closed_.load(std::memory_order_acquire))
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    return net::InetAddress{
        net::sockets::getLocalAddr(state_->fd_.load(std::memory_order_acquire))};
}

/// 是否打开。线程安全。
bool AsyncAcceptor::isOpen() const noexcept
{
    return state_ && !state_->closed_.load(std::memory_order_acquire);
}

/// 公开接口：委托给 acceptImpl，转移 state_ 的 shared_ptr 副本。
Task<AcceptedConnection> AsyncAcceptor::accept()
{
    return acceptImpl(state_);
}

/**
 * @brief accept 的实现体。
 *
 * 若 state 为空，抛 bad_file_descriptor。
 * 否则 co_await 一个 Awaiter，由其决定立即返回还是挂起。
 */
Task<AcceptedConnection> AsyncAcceptor::acceptImpl(std::shared_ptr<State> state)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    co_return co_await State::Awaiter{std::move(state)};
}

/**
 * @brief 线程安全的关闭。
 *
 * 若在 loop 线程：直接调用 closeInLoop。
 * 否则：通过 queueInLoop 把 state 投递到 loop 线程执行 closeInLoop，
 * 并 wakeup 唤醒 loop。
 */
void AsyncAcceptor::close()
{
    auto state = std::exchange(state_, {});
    if (!state)
    {
        return;
    }

    if (state->loop_.isInLoopThread())
    {
        state->closeInLoop();
    }
    else
    {
        net::EventLoop* loop = &state->loop_;
        loop->queueInLoop([state = std::move(state)] { state->closeInLoop(); });
        loop->wakeup();
    }
}

}  // namespace chaoxi::coro