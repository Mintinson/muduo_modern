#include "chaoxi/coro/AsyncFd.hpp"

#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <atomic>
#include <cerrno>
#include <coroutine>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace chaoxi::coro
{

/**
 * @brief AsyncFd 的共享状态。
 *
 * 为什么需要 shared_ptr<State>？
 *   - 协程可能在 AsyncFd 对象被销毁后仍然挂起（例如用户 co_await waitReadable()
 *     后丢弃了 AsyncFd）。Awaiter 持有 shared_ptr<State>，保证 State 存活到协程恢复。
 *   - Channel 的回调通过 weak_ptr 访问 State，避免循环引用。
 *
 * 职责：
 *   - 持有 EventLoop、fd、Channel、所有权语义。
 *   - 管理读/写两个方向的等待者（每次每个方向最多一个）。
 *   - 通过 Channel 注册 fd 事件，在事件触发时唤醒等待者。
 *   - 提供线程安全的 close。
 *
 * 线程模型：
 *   - 除 closed_/fd_ 外，所有成员都只在 loop 线程访问。
 *   - closed_ 和 fd_ 是原子变量，可在任意线程读取。
 */
struct AsyncFd::State : std::enable_shared_from_this<State>
{
    /// 等待方向。
    enum class Direction : std::uint8_t
    {
        read,
        write,
    };

    /**
     * @brief 单个等待者的信息。
     *
     * 存储在 State 中，由 Awaiter 通过指针引用。
     * 协程句柄在等待期间有效；错误码在完成时填入。
     */
    struct Waiter
    {
        std::coroutine_handle<> coroutine{};  ///< 等待中的协程句柄。
        std::error_code error;                ///< 完成时的错误码。
    };

    /**
     * @brief 可被 co_await 的等待器。
     *
     * 生命周期：
     *   - 构造时持有 shared_ptr<State>，保证等待期间 State 存活。
     *   - 析构时如果仍在等待，会调用 cancelWaiter 清理槽位（例如协程被销毁）。
     *
     * await 流程：
     *   1. await_ready 返回 false，总是挂起。
     *   2. `await_suspend` 检查 closed 和是否已有等待者，注册到 State 的 slot，
     *      并使能对应方向的 Channel 事件。返回 true 表示挂起成功，
     *      返回 false 表示不挂起（错误已存入 waiter_.error）。
     *   3. await_resume 检查错误码，若有则抛 std::system_error。
     */
    class Awaiter
    {
    public:
        Awaiter(std::shared_ptr<State> state, Direction direction) noexcept
            : state_(std::move(state))
            , direction_(direction)
        {
        }

        /// 析构：若仍在等待，从 State 中注销自己，避免悬垂指针。
        ~Awaiter()
        {
            if (waiter_.coroutine)
            {
                state_->cancelWaiter(direction_, &waiter_);
            }
        }

        // Awaiter 不可拷贝、不可移动：它的地址被 State 以指针形式持有。
        Awaiter(const Awaiter&) = delete;
        Awaiter(Awaiter&&) noexcept = delete;
        Awaiter& operator=(const Awaiter&) = delete;
        Awaiter& operator=(Awaiter&&) noexcept = delete;

        /// 总是挂起，由 await_suspend 决定是否真正注册等待。
        [[nodiscard]] bool
        await_ready()  // NOLINT(readability-convert-member-functions-to-static)
            const noexcept
        {
            return false;
        }

        /**
         * @brief 挂起当前协程，注册到 State。
         * @param coroutine 当前协程句柄。
         * @return true  表示成功挂起，协程将在事件触发时被恢复。
         *         false 表示不挂起，await_resume 会立刻执行并可能抛错。
         *
         * 必须在 loop 线程调用。
         * 检查顺序：
         *   1. State 是否已关闭 → 返回 bad_file_descriptor。
         *   2. 该方向是否已有等待者 → 返回 operation_in_progress。
         *   3. 注册自己，使能 Channel 事件。
         */
        bool await_suspend(std::coroutine_handle<> coroutine)
        {
            state_->loop_.assertInLoopThread();
            waiter_.coroutine = coroutine;

            // 如果 State 已关闭，直接失败，不挂起。
            if (state_->closed_.load(std::memory_order_acquire))
            {
                waiter_.coroutine = {};
                waiter_.error =
                    std::make_error_code(std::errc::bad_file_descriptor);
                return false;
            }

            // 每个方向只允许一个等待者，避免复杂度和竞态。
            Waiter*& slot = state_->waiter(direction_);
            if (slot != nullptr)
            {
                waiter_.coroutine = {};
                waiter_.error =
                    std::make_error_code(std::errc::operation_in_progress);
                return false;
            }

            // 注册自己，并使能对应方向的 Channel 事件。
            slot = &waiter_;
            state_->enable(direction_);
            return true;
        }

        /**
         * @brief 恢复后执行：如果等待期间收到错误，则抛出。
         */
        void await_resume()  // NOLINT(readability-make-member-function-const)
        {
            if (waiter_.error)
            {
                throw std::system_error(waiter_.error);
            }
        }

    private:
        std::shared_ptr<State> state_;  ///< 保证 State 在等待期间存活。
        Direction direction_;           ///< 等待方向。
        Waiter waiter_;                 ///< 等待者信息，地址被 State 持有。
    };

    /**
     * @brief 构造 State。
     * @param loop      目标 EventLoop。
     * @param fd        文件描述符。
     * @param ownership 所有权语义。
     *
     * 必须在 loop 线程调用。fd 无效则抛 bad_file_descriptor。
     */
    State(net::EventLoop& loop, net::SocketHandle fd, FdOwnership ownership)
        : loop_(loop)
        , fd_(fd)
        , ownership_(ownership)
        , channel_(&loop, fd)
    {
        if (fd == net::kInvalidSocket)
        {
            throw std::system_error(
                std::make_error_code(std::errc::bad_file_descriptor));
        }
        loop_.assertInLoopThread();
    }

    /**
     * @brief 工厂方法：创建 State 并初始化 Channel 回调。
     *
     * 使用 shared_from_this 需要对象已由 shared_ptr 管理，
     * 因此不能在构造函数中调用 initializeCallbacks，必须分两步。
     */
    static std::shared_ptr<State> create(net::EventLoop& loop,
                                         net::SocketHandle fd,
                                         FdOwnership ownership)
    {
        auto state = std::make_shared<State>(loop, fd, ownership);
        state->initializeCallbacks();
        return state;
    }

    /**
     * @brief 初始化 Channel 的读/写/关闭/错误回调。
     *
     * 所有回调都通过 weak_ptr 访问 State，避免 State 与 Channel 之间的循环引用。
     * 回调只负责调用 complete()，由 complete() 决定如何唤醒等待者。
     */
    void initializeCallbacks()
    {
        std::weak_ptr<State> weakState = shared_from_this();

        // 把 Channel 与 State 绑定，确保 Channel 存活期间 State 不被销毁。
        // 具体 tie 的语义由 Channel 实现决定，通常用于延长生命周期或建立关联。
        channel_.tie(shared_from_this());

        // 可读事件：唤醒读等待者。
        channel_.setReadCallback(
            [weakState](Timestamp)
            {
                if (auto state = weakState.lock())
                {
                    state->complete(Direction::read, {});
                }
            });

        // 可写事件：唤醒写等待者。
        channel_.setWriteCallback(
            [weakState]
            {
                if (auto state = weakState.lock())
                {
                    state->complete(Direction::write, {});
                }
            });

        // 关闭事件：对端关闭或本地关闭。
        // 通过 getsockopt(SO_ERROR) 判断是否有具体错误：
        //   - 有错误：读写两个方向都以该错误完成。
        //   - 无错误：读方向正常完成（EOF），写方向以 broken_pipe 完成。
        channel_.setCloseCallback(
            [weakState]
            {
                if (auto state = weakState.lock())
                {
                    const int error = net::sockets::getSocketError(
                        state->fd_.load(std::memory_order_acquire));
                    if (error != 0)
                    {
                        const std::error_code code(error,
                                                   std::generic_category());
                        state->complete(Direction::read, code);
                        state->complete(Direction::write, code);
                    }
                    else
                    {
                        state->complete(Direction::read, {});
                        state->complete(
                            Direction::write,
                            std::make_error_code(std::errc::broken_pipe));
                    }
                }
            });

        // 错误事件：读写两个方向都以该错误完成。
        // 若 SO_ERROR 为 0，则用 EIO 兜底。
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
                    const std::error_code code(error, std::generic_category());
                    state->complete(Direction::read, code);
                    state->complete(Direction::write, code);
                }
            });
    }

    /// 根据方向返回对应的等待者槽位引用。
    [[nodiscard]] Waiter*& waiter(Direction direction) noexcept
    {
        return direction == Direction::read ? readWaiter_ : writeWaiter_;
    }

    /**
     * @brief 使能指定方向的 Channel 事件。
     *
     * 只在尚未使能时调用，避免重复注册。
     * registered_ 标记 Channel 是否已注册到 EventLoop，用于 close 时 remove。
     */
    void enable(Direction direction)
    {
        if (direction == Direction::read)
        {
            if (!channel_.isReading())
            {
                channel_.enableReading();
                registered_ = true;
            }
        }
        else if (!channel_.isWriting())
        {
            channel_.enableWriting();
            registered_ = true;
        }
    }

    /// 关闭指定方向的 Channel 事件。
    void disable(Direction direction)
    {
        if (direction == Direction::read)
        {
            if (channel_.isReading())
            {
                channel_.disableReading();
            }
        }
        else if (channel_.isWriting())
        {
            channel_.disableWriting();
        }
    }

    /**
     * @brief 完成某个方向的等待：唤醒等待者并传入错误码。
     *
     * 流程：
     *   1. 取出槽位，若为空则无事可做。
     *   2. 关闭该方向的 Channel 事件。
     *   3. 把错误码写入 Waiter。
     *   4. 通过 queueInLoop 投递恢复任务，而不是直接 resume。
     *      原因：避免在 Channel 事件处理中直接 resume 导致重入或迭代器失效。
     *
     * 必须在 loop 线程调用。
     */
    void complete(Direction direction, std::error_code error)
    {
        loop_.assertInLoopThread();
        Waiter*& slot = waiter(direction);
        if (slot == nullptr)
        {
            return;
        }

        Waiter* completed = std::exchange(slot, nullptr);
        disable(direction);
        completed->error = error;
        auto coroutine = std::exchange(completed->coroutine, {});
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
     * @brief 取消等待者（通常由 Awaiter 析构调用）。
     *
     * 只有当 slot 仍指向 candidate 时才清理，避免误清除已被 complete 的槽位。
     * 清理后关闭对应方向的 Channel 事件，并置空协程句柄。
     *
     * 必须在 loop 线程调用。
     */
    void cancelWaiter(Direction direction, Waiter* candidate)
    {
        loop_.assertInLoopThread();
        Waiter*& slot = waiter(direction);
        if (slot == candidate)
        {
            slot = nullptr;
            disable(direction);
        }
        candidate->coroutine = {};
    }

    /**
     * @brief 在 loop 线程中执行关闭。
     *
     * 步骤：
     *   1. 原子设置 closed_，若已关闭则返回。
     *   2. 以 operation_canceled 完成读写两个方向的等待者。
     *   3. 从 EventLoop 中移除 Channel。
     *   4. 若 ownership 为 owned，关闭 fd。
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

        // 唤醒所有等待者，通知它们操作已取消。
        const auto cancelled =
            std::make_error_code(std::errc::operation_canceled);
        complete(Direction::read, cancelled);
        complete(Direction::write, cancelled);

        // 从 EventLoop 中移除 Channel，避免悬垂事件。
        if (registered_)
        {
            if (!channel_.isNoneEvent())
            {
                channel_.disableAll();
            }
            channel_.remove();
            registered_ = false;
        }

        // 原子取出 fd，若拥有所有权则关闭。
        const net::SocketHandle fd =
            fd_.exchange(net::kInvalidSocket, std::memory_order_acq_rel);
        if (ownership_ == FdOwnership::owned && fd != net::kInvalidSocket)
        {
            net::sockets::close(fd);
        }
    }

    net::EventLoop& loop_;                        ///< 目标 EventLoop。
    std::atomic<net::SocketHandle> fd_;           ///< 底层 fd，可跨线程读取。
    FdOwnership ownership_;                       ///< 所有权语义。
    net::Channel channel_;                        ///< 用于注册 fd 事件。
    std::atomic_bool closed_{false};              ///< 是否已关闭，可跨线程读取。
    bool registered_ = false;                     ///< Channel 是否已注册到 EventLoop。
    Waiter* readWaiter_ = nullptr;                ///< 当前读等待者，仅 loop 线程访问。
    Waiter* writeWaiter_ = nullptr;               ///< 当前写等待者，仅 loop 线程访问。
};

/**
 * @brief 公开构造函数：创建 State 并初始化。
 *
 * 必须在 loop 线程调用。
 */
AsyncFd::AsyncFd(net::EventLoop& loop,
                 net::SocketHandle fd,
                 FdOwnership ownership)
    : state_(State::create(loop, fd, ownership))
{
}

/// 私有构造：从已有 State 构造，通常由内部使用。
AsyncFd::AsyncFd(std::shared_ptr<State> state) noexcept
    : state_(std::move(state))
{
}

/// 析构：调用 close()，确保资源被释放。
AsyncFd::~AsyncFd()
{
    close();
}

AsyncFd::AsyncFd(AsyncFd&& other) noexcept : state_(std::move(other.state_)) {}

AsyncFd& AsyncFd::operator=(AsyncFd&& other) noexcept
{
    if (this != &other)
    {
        close();  // 先释放当前资源。
        state_ = std::move(other.state_);
    }
    return *this;
}

/// 返回底层 fd；若无 state 或已关闭，返回 kInvalidSocket。线程安全。
net::SocketHandle AsyncFd::nativeHandle() const noexcept
{
    return state_ ? state_->fd_.load(std::memory_order_acquire)
                  : net::kInvalidSocket;
}

/// 是否打开。线程安全。
bool AsyncFd::isOpen() const noexcept
{
    return state_ && !state_->closed_.load(std::memory_order_acquire);
}

/// 返回关联的 EventLoop；若无 state 则抛 std::logic_error。
net::EventLoop& AsyncFd::eventLoop() const
{
    if (!state_)
    {
        throw std::logic_error("AsyncFd has no EventLoop");
    }
    return state_->loop_;
}

/// 公开接口：委托给静态实现，转移 state_ 的 shared_ptr 副本。
Task<void> AsyncFd::waitReadable()
{
    return waitReadableImpl(state_);
}

Task<void> AsyncFd::waitWritable()
{
    return waitWritableImpl(state_);
}

Task<std::size_t> AsyncFd::readSome(std::span<std::byte> buffer)
{
    return readSomeImpl(state_, buffer);
}

Task<void> AsyncFd::readExactly(std::span<std::byte> buffer)
{
    return readExactlyImpl(state_, buffer);
}

Task<std::size_t> AsyncFd::writeSome(std::span<const std::byte> buffer)
{
    return writeSomeImpl(state_, buffer);
}

Task<void> AsyncFd::writeAll(std::span<const std::byte> buffer)
{
    return writeAllImpl(state_, buffer);
}

/**
 * @brief 等待可读的实现。
 *
 * 若 state 为空，说明 AsyncFd 已被关闭或未初始化，抛 bad_file_descriptor。
 * 否则 co_await 一个读方向的 Awaiter。
 */
Task<void> AsyncFd::waitReadableImpl(std::shared_ptr<State> state)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    co_await State::Awaiter{std::move(state), State::Direction::read};
}

/// 等待可写的实现，同上。
Task<void> AsyncFd::waitWritableImpl(std::shared_ptr<State> state)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    co_await State::Awaiter{std::move(state), State::Direction::write};
}

/**
 * @brief 读取最多 buffer.size() 字节。
 *
 * 流程：
 *   1. 检查 state 和 buffer。
 *   2. 循环调用非阻塞 read：
 *      - result >= 0：成功，返回读取字节数（可能为 0，表示 EOF）。
 *      - EINTR：被信号中断，重试。
 *      - EAGAIN/EWOULDBLOCK：数据未就绪，co_await 等待可读，然后重试。
 *      - 其他 errno：抛 std::system_error。
 *
 * 必须在 loop 线程执行。
 */
Task<std::size_t> AsyncFd::readSomeImpl(std::shared_ptr<State> state,
                                        std::span<std::byte> buffer)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    state->loop_.assertInLoopThread();
    if (buffer.empty())
    {
        co_return 0;
    }

    while (true)
    {
        const net::SocketHandle fd = state->fd_.load(std::memory_order_acquire);
        const net::SignedSize result =
            net::sockets::read(fd, buffer.data(), buffer.size());
        if (result >= 0)
        {
            co_return static_cast<std::size_t>(result);
        }
        if (errno == EINTR)
        {
            continue;  // 被信号打断，重试。
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 数据未就绪，挂起等待可读事件，事件触发后重试。
            co_await State::Awaiter{state, State::Direction::read};
            continue;
        }
        // 其他错误：直接抛出。
        throw std::system_error(errno, std::generic_category());
    }
}

/**
 * @brief 写入最多 buffer.size() 字节。
 *
 * 与 readSomeImpl 对称，区别在于等待方向为 write。
 */
Task<std::size_t> AsyncFd::writeSomeImpl(std::shared_ptr<State> state,
                                         std::span<const std::byte> buffer)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    state->loop_.assertInLoopThread();
    if (buffer.empty())
    {
        co_return 0;
    }

    while (true)
    {
        const net::SocketHandle fd = state->fd_.load(std::memory_order_acquire);
        const net::SignedSize result =
            net::sockets::write(fd, buffer.data(), buffer.size());
        if (result >= 0)
        {
            co_return static_cast<std::size_t>(result);
        }
        if (errno == EINTR)
        {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            co_await State::Awaiter{state, State::Direction::write};
            continue;
        }
        throw std::system_error(errno, std::generic_category());
    }
}

/**
 * @brief 读取恰好 buffer.size() 字节。
 *
 * 循环调用 readSomeImpl，累加读取字节数。
 * 若某次返回 0（对端关闭），抛 connection_reset。
 */
Task<void> AsyncFd::readExactlyImpl(std::shared_ptr<State> state,
                                    std::span<std::byte> buffer)
{
    std::size_t read = 0;
    while (read < buffer.size())
    {
        const std::size_t count =
            co_await readSomeImpl(state, buffer.subspan(read));
        if (count == 0)
        {
            throw std::system_error(
                std::make_error_code(std::errc::connection_reset));
        }
        read += count;
    }
}

/**
 * @brief 写入恰好 buffer.size() 字节。
 *
 * 循环调用 writeSomeImpl，累加写入字节数。
 * 若某次返回 0，抛 io_error（通常不应该发生）。
 */
Task<void> AsyncFd::writeAllImpl(std::shared_ptr<State> state,
                                 std::span<const std::byte> buffer)
{
    std::size_t written = 0;
    while (written < buffer.size())
    {
        const std::size_t count =
            co_await writeSomeImpl(state, buffer.subspan(written));
        if (count == 0)
        {
            throw std::system_error(std::make_error_code(std::errc::io_error));
        }
        written += count;
    }
}

/**
 * @brief 线程安全的关闭。
 *
 * 流程：
 *   1. 取出 state_，若为空则已关闭。
 *   2. 若当前在 loop 线程：直接调用 closeInLoop。
 *   3. 否则：通过 queueInLoop 把 state 投递到 loop 线程执行 closeInLoop，
 *      并 wakeup 唤醒 loop。
 *
 * 注意：捕获的是 shared_ptr<State>，确保 State 在 lambda 执行前不被销毁。
 */
void AsyncFd::close()
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