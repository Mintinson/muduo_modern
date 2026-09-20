#pragma once

#include "chaoxi/coro/Task.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <coroutine>
#include <exception>
#include <functional>
#include <utility>

namespace chaoxi::coro
{

/**
 * @brief 协程异常处理器类型。
 *
 * 使用 std::move_only_function 而不是 std::function，是为了支持捕获
 * move-only 资源（例如 std::unique_ptr、std::promise 等）。
 * 默认构造为空，表示没有处理器；此时若协程抛出异常，将直接 std::terminate()。
 */
using CoroutineExceptionHandler =
    std::move_only_function<void(std::exception_ptr)>;

namespace detail
{
/**
 * @brief 一个“分离式”协程的返回对象。
 *
 * DetachedTask 用于包装一个顶层协程，该协程：
 *   - 惰性启动（initial_suspend 挂起），以便由 EventLoop 调度到目标线程；
 *   - 结束时自动销毁（final_suspend 不挂起），无需外部手动 destroy；
 *   - 内部异常已在 detach() 中捕获，若仍有异常逃逸则直接终止程序。
 *
 * 它只负责持有协程句柄，并提供 release() 把句柄所有权交出去。
 */
class DetachedTask
{
public:
    struct promise_type
    {
        /// 从 promise 自身构造 DetachedTask，内部保存协程句柄。
        DetachedTask get_return_object() noexcept
        {
            // 从 promise 自身构造
            // DetachedTask，内部保存协程句柄。这是协程返回给调用者的对象。
            return DetachedTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /// 惰性启动：创建协程后先挂起，等待被 resume。
        /// 这样 spawn() 才能先把句柄投递到 EventLoop 队列，再在 loop 线程恢复。
        [[nodiscard]] std::suspend_always initial_suspend() const noexcept
        {
            // 返回 suspend_always，确保调用 detail::detach(...)
            // 时，协程体不会立即执行，只是创建帧并返回 DetachedTask。
            return {};
        }

        /// 协程结束时不再挂起，直接自动销毁协程帧。
        /// 这是“detached”语义的核心：没有人拥有它，它必须自己清理自己。
        [[nodiscard]] std::suspend_never final_suspend() const noexcept
        {
            // 协程结束时
            // 不挂起，直接自动销毁协程帧。这是“detached”的核心：没有人需要手动
            // destroy() 它，它自己清理自己。
            return {};
        }

        /// 协程没有返回值，必须提供 return_void。
        void return_void() const noexcept {}

        /// 安全网：detach() 内部已经用 try/catch 捕获了 co_await 的异常。
        /// 如果还有异常逃逸到这里（例如异常处理器本身抛异常），说明程序状态已不可恢复，
        /// 直接终止，避免未定义行为。
        void unhandled_exception() const noexcept { std::terminate(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    /// 显式构造函数：从协程句柄构造，通常只由 promise_type::get_return_object
    /// 调用。
    explicit DetachedTask(handle_type coroutine) noexcept : coroutine_(coroutine)
    {
    }

    DetachedTask(DetachedTask&& other) noexcept
        : coroutine_(std::exchange(other.coroutine_, {}))
    {
    }

    DetachedTask(const DetachedTask&) = delete;
    DetachedTask& operator=(const DetachedTask&) = delete;
    DetachedTask& operator=(DetachedTask&&) = delete;

    ~DetachedTask()
    {
        if (coroutine_)
        {
            coroutine_.destroy();
        }
    }

    /**
     * @brief 释放句柄所有权。
     *
     * 返回内部句柄并将内部置空，这样 DetachedTask 析构时不会销毁协程。
     * spawn() 需要把句柄捕获到 lambda 中交给 EventLoop，因此必须先 release。
     */
    [[nodiscard]] handle_type release() noexcept
    {
        // 因为 spawn 要把句柄捕获到 lambda 中，交给 EventLoop 队列。DetachedTask
        // 本身是栈上临时对象，必须在它析构前释放所有权，否则它会销毁协程。
        return std::exchange(coroutine_, {});
    }

private:
    handle_type coroutine_;
};

/**
 * @brief 顶层驱动协程：等待一个 Task<void> 并处理其异常。
 *
 * 这个协程本身返回 DetachedTask，因此它是自销毁的。
 * 它的职责：
 *   1. 拥有传入的 Task<void> 和异常处理器；
 *   2. 通过 co_await 驱动 Task 执行；
 *   3. 捕获 Task 抛出的任何异常，转交给异常处理器；
 *   4. 若没有处理器，则终止程序。
 *
 * 注意：必须使用 std::move(task)，因为 Task 是 move-only，
 * 且其 operator co_await() 只对右值开放。
 */
inline DetachedTask detach(Task<void> task,
                           CoroutineExceptionHandler exceptionHandler)
{
    try
    {
        // 驱动 Task 执行。
        // co_await std::move(task) 会把 Task 内部的协程句柄转移到 Awaiter，由
        // Awaiter 负责在等待结束后销毁 Task 的协程帧。
        co_await std::move(task);
    }
    catch (...)
    {
        if (exceptionHandler)
        {
            exceptionHandler(std::current_exception());
        }
        else
        {
            std::terminate();
        }
    }
}

}  // namespace detail

/**
 * @brief 在指定的 EventLoop 上启动一个 Task<void>（发射后不管）。
 *
 * 执行流程：
 *   1. 创建 detail::detach 协程，它会在内部 co_await 目标 Task；
 *   2. 因为 DetachedTask 的 initial_suspend 是 suspend_always，
 *      此时协程挂起，不会立即执行；
 *   3. 通过 release() 取出协程句柄，使 DetachedTask 临时对象不再拥有它；
 *   4. 把句柄投递到 EventLoop 的队列中，并唤醒 loop；
 *   5. loop 线程稍后执行 lambda，恢复协程，从而开始运行目标 Task。
 *
 * @param loop              目标 EventLoop，协程将在其线程上首次恢复。
 * @param task              要启动的 Task<void>，所有权转移给 spawn。
 * @param exceptionHandler  可选的异常处理器；若为空且发生异常，则
 * std::terminate()。
 */
inline void spawn(net::EventLoop& loop,
                  Task<void> task,
                  CoroutineExceptionHandler exceptionHandler = {})
{
    // 创建 detach 协程。此时协程挂起在 initial_suspend，尚未执行。
    auto detached = detail::detach(std::move(task), std::move(exceptionHandler));

    // 取出协程句柄，detached 变为空。
    // 这样 detached 析构时不会销毁协程，所有权转交给下面的 lambda。
    auto coroutine = detached.release();

    // 把恢复协程的操作投递到 EventLoop 的待执行队列。
    // lambda 捕获裸句柄，稍后在 loop 线程中执行。
    loop.queueInLoop(
        [coroutine]
        {
            // 防御性检查：句柄有效且协程尚未完成。
            // 由于 initial_suspend 挂起，首次 resume 前 done() 必为 false。
            if (coroutine && !coroutine.done())
            {
                // 恢复协程，开始执行 detach 协程体，进而驱动目标 Task。
                coroutine.resume();
            }
        });
    // 唤醒 EventLoop，确保它即使正在阻塞也能及时处理新任务。
    loop.wakeup();
}

}  // namespace chaoxi::coro
