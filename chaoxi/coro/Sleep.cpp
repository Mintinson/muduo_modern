#include "chaoxi/coro/Sleep.hpp"

#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TimerId.hpp"

#include <coroutine>
#include <memory>

namespace chaoxi::coro
{
namespace
{
/**
 * @brief 休眠等待的共享状态。
 *
 * 之所以用 shared_ptr 管理，是因为定时器回调可能比 SleepAwaiter 存活更久。
 * SleepAwaiter 持有 shared_ptr，回调持有 weak_ptr，从而安全检测对象是否已销毁。
 */
struct SleepState
{
    std::coroutine_handle<> coroutine{};  ///< 等待中的协程句柄。
    bool scheduled = false;  ///< 定时器已触发，恢复任务已投递到 queueInLoop。
    bool resumed = false;    ///< 恢复任务已执行，协程已被恢复。
};

/**
 * @brief 用于 sleepUntil 的 awaitable。
 *
 * 负责注册定时器，并在到期后恢复协程。支持提前取消（析构时）。
 */
class SleepAwaiter
{
public:
    /**
     * @brief 构造 SleepAwaiter。
     * @param loop     目标 EventLoop。
     * @param deadline 绝对截止时间。
     */
    SleepAwaiter(net::EventLoop& loop, Timestamp deadline)
        : loop_(loop)
        , deadline_(deadline)
        , state_(std::make_shared<SleepState>())
    {
    }

    /**
     * @brief 析构函数：处理提前取消。
     *
     * 如果协程尚未恢复（resumed == false），说明等待被提前终止。
     * 此时：
     *   1. 将 state_->coroutine 置空，防止后续回调 resume 一个已销毁的协程；
     *   2. 如果定时器已注册，取消它。
     */
    ~SleepAwaiter()
    {
        if (!state_->resumed)
        {
            state_->coroutine = {};
            if (timerRegistered_)
            {
                loop_.cancel(timer_);
            }
        }
    }

    /**
     * @brief 判断是否已经到期。
     * @return true 表示无需挂起，直接执行 await_resume()。
     */
    [[nodiscard]] bool await_ready() const noexcept
    {
        return deadline_ <= Timestamp::clock::now();
    }

    /**
     * @brief 挂起当前协程，并注册定时器。
     *
     * 必须在 loop 线程调用。保存协程句柄，注册定时器。
     * 定时器到期后，通过 queueInLoop 投递恢复任务，避免在定时器处理中直接
     * resume。
     *
     * @param coroutine 当前等待的协程句柄。
     */
    void await_suspend(std::coroutine_handle<> coroutine)
    {
        // 确保在 loop 线程注册定时器。
        loop_.assertInLoopThread();

        // 保存协程句柄，供定时器回调恢复。
        state_->coroutine = coroutine;

        // 使用 weak_ptr 给定时器回调，避免延长 SleepState 生命周期。
        std::weak_ptr<SleepState> weakState = state_;

        // 注册定时器。
        timer_ =
            loop_.runAt(deadline_,
                        [weakState, &loop = loop_]
                        {
                            // 尝试锁定状态；如果 SleepAwaiter 已析构，state
                            // 为空，直接返回。
                            auto state = weakState.lock();
                            if (!state || !state->coroutine || state->scheduled)
                            {
                                // 状态已失效、协程句柄为空、或已经调度过，均不处理。
                                return;
                            }

                            // 标记定时器已触发，防止重复调度。
                            state->scheduled = true;

                            // 通过 queueInLoop
                            // 投递恢复任务，避免在定时器回调中直接 resume。
                            loop.queueInLoop(
                                [state]
                                {
                                    // 再次检查：句柄是否有效，是否已经恢复过。
                                    if (!state->coroutine || state->resumed)
                                    {
                                        return;
                                    }

                                    // 标记已恢复，防止重复 resume。
                                    state->resumed = true;

                                    // 取出句柄并置空，然后恢复协程。
                                    auto suspended = state->coroutine;
                                    state->coroutine = {};
                                    suspended.resume();
                                });
                        });

        // 标记定时器已注册，供析构时取消。
        timerRegistered_ = true;
    }

    /**
     * @brief 恢复后执行，无返回值。
     */
    void await_resume() const noexcept {}

private:
    net::EventLoop& loop_;  ///< 目标 EventLoop。
    Timestamp deadline_;    ///< 绝对截止时间。
    std::shared_ptr<SleepState>
        state_;                     ///< 共享状态，定时器回调通过 weak_ptr 访问。
    net::TimerId timer_;            ///< 定时器 ID，用于取消。
    bool timerRegistered_ = false;  ///< 是否已成功注册定时器。
};

}  // namespace

/**
 * @brief 实现 sleepUntil。
 *
 * 创建一个 SleepAwaiter 并 co_await 它。由于 Task 是惰性的，
 * 这个协程在调用时不会执行，直到被等待。
 */
Task<void> sleepUntil(net::EventLoop& loop, Timestamp deadline)
{
    co_await SleepAwaiter{loop, deadline};
}

}  // namespace chaoxi::coro
