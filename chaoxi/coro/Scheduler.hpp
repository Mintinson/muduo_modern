#pragma once

#include "chaoxi/net/EventLoop.hpp"

#include <coroutine>

namespace chaoxi::coro
{
/**
 * @brief 将当前协程切换到指定 EventLoop 线程上继续执行。
 *
 * 使用方式：
 *   co_await scheduleOn(loop);
 *
 * 行为：
 *   - 如果当前已经在目标 loop 线程，await_ready 返回 true，直接继续，不挂起。
 *   - 否则，在 await_suspend 中把协程恢复任务投递到目标 loop 的队列，
 *     并唤醒 loop，确保它及时处理。
 */
class ScheduleOn
{
public:
    /**
     * @brief 构造 ScheduleOn。
     * @param loop 目标 EventLoop，必须保证在协程恢复前存活。
     */
    explicit ScheduleOn(net::EventLoop& loop) noexcept : loop_(loop) {}

    /**
     * @brief 判断是否需要挂起。
     * @return true 表示当前已在目标 loop 线程，无需挂起。
     *
     * 这是一个优化：避免不必要的队列投递和线程切换。
     */
    [[nodiscard]] bool await_ready() const noexcept
    {
        return loop_.isInLoopThread();
    }

    /**
     * @brief 挂起当前协程，并把恢复任务投递到目标 EventLoop。
     * @param coroutine 当前等待的协程句柄。
     *
     * 注意：
     *   - 此函数可能从非 loop 线程调用，因此必须使用线程安全的 queueInLoop。
     *   - 必须调用 wakeup() 唤醒可能正在阻塞的 loop。
     *   - 这里没有对 coroutine 的生命周期做保护，调用者需确保协程在恢复前存活。
     */
    void await_suspend(std::coroutine_handle<> coroutine) const
    {
        loop_.queueInLoop([coroutine] { coroutine.resume(); });
        loop_.wakeup();
    }

    /**
     * @brief 恢复后执行，无返回值。
     */
    void await_resume() const noexcept {}

private:
    net::EventLoop& loop_;
};

/**
 * @brief 主动让出执行权，把当前协程的恢复任务重新排到同一 EventLoop 的队列尾部。
 *
 * 使用方式：
 *   co_await yield(loop);
 *
 * 行为：
 *   - 总是挂起（await_ready 返回 false），即使已经在 loop 线程。
 *   - 在 await_suspend 中把恢复任务投递到当前 loop 的队列，不调用 wakeup。
 *   - 这样 loop 会先处理其他已就绪的事件，然后再恢复当前协程。
 */
class YieldToEventLoop
{
public:
    /**
     * @brief 构造 YieldToEventLoop。
     * @param loop 目标 EventLoop，通常就是当前协程所在的 loop。
     */
    explicit YieldToEventLoop(net::EventLoop& loop) noexcept : loop_(loop) {}

    /**
     * @brief 总是返回 false，表示一定要挂起。
     *
     * 这是“让出”的语义：即使已经在 loop 线程，也要把恢复任务排到队列尾部，
     * 让 loop 有机会处理其他事件。
     */
    [[nodiscard]] bool await_ready() const noexcept { return false; }

    /**
     * @brief 挂起当前协程，并把恢复任务投递到同一 EventLoop 的队列尾部。
     * @param coroutine 当前等待的协程句柄。
     *
     * 注意：
     *   - 此函数一定在 loop 线程被调用（因为 co_await yield(loop) 通常发生在
     * loop 线程上的协程中）。
     *   - 因此不需要调用 wakeup()，loop
     * 本来就是活跃的，队列中的任务会在当前事件处理完后被处理。
     *   - 同样没有对 coroutine 的生命周期做保护。
     */
    void await_suspend(std::coroutine_handle<> coroutine) const
    {
        loop_.queueInLoop([coroutine] { coroutine.resume(); });
    }

    /**
     * @brief 恢复后执行，无返回值。
     */
    void await_resume() const noexcept {}

private:
    net::EventLoop& loop_;
};

/**
 * @brief 工厂函数：创建一个 ScheduleOn awaitable。
 * @param loop 目标 EventLoop。
 * @return ScheduleOn 对象，用于 co_await。
 *
 * [[nodiscard]] 提醒调用者不要忽略返回值，否则 co_await 不会发生。
 */
[[nodiscard]] inline ScheduleOn scheduleOn(net::EventLoop& loop) noexcept
{
    return ScheduleOn{loop};
}

/**
 * @brief 工厂函数：创建一个 YieldToEventLoop awaitable。
 * @param loop 目标 EventLoop。
 * @return YieldToEventLoop 对象，用于 co_await。
 *
 * [[nodiscard]] 提醒调用者不要忽略返回值。
 */
[[nodiscard]] inline YieldToEventLoop yield(net::EventLoop& loop) noexcept
{
    return YieldToEventLoop{loop};
}

}  // namespace chaoxi::coro
