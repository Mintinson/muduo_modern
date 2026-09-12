#pragma once

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/coro/Task.hpp"

#include <chrono>

namespace chaoxi::net
{
class EventLoop;
}  // namespace chaoxi::net

namespace chaoxi::coro
{
/**
 * @brief 在指定 EventLoop 上休眠，直到 deadline 时间点。
 *
 * 返回一个惰性的 Task<void>，只有在被 co_await 或 spawn 时才会真正挂起。
 * 内部使用 SleepAwaiter 注册定时器，到期后恢复协程。
 *
 * @param loop     目标 EventLoop，必须在 loop 线程上等待。
 * @param deadline 绝对截止时间。
 * @return Task<void> 惰性任务。
 */
Task<void> sleepUntil(net::EventLoop& loop, Timestamp deadline);

/**
 * @brief 在指定 EventLoop 上休眠一段时长。
 *
 * 便捷模板：计算 now() + duration 得到 deadline，然后调用 sleepUntil。
 *
 * @tparam Rep    时长表示类型。
 * @tparam Period 时长周期类型。
 * @param loop     目标 EventLoop。
 * @param duration 休眠时长。
 * @return Task<void> 惰性任务。
 */
template <typename Rep, typename Period>
Task<void> sleepFor(net::EventLoop& loop,
                    std::chrono::duration<Rep, Period> duration)
{
    // 计算绝对截止时间，并转换为 Timestamp::duration 精度。
    const auto deadline =
        Timestamp::clock::now() +
        std::chrono::duration_cast<Timestamp::duration>(duration);
    // 委托给 sleepUntil，保持惰性。
    return sleepUntil(loop, deadline);
}

}  // namespace chaoxi::coro
