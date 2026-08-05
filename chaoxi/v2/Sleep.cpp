#include "chaoxi/v2/Sleep.hpp"

#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TimerId.hpp"

#include <coroutine>
#include <memory>

namespace chaoxi::v2
{
namespace
{

struct SleepState
{
    std::coroutine_handle<> coroutine{};
    bool scheduled = false;
    bool resumed = false;
};

class SleepAwaiter
{
public:
    SleepAwaiter(net::EventLoop& loop, Timestamp deadline)
        : loop_(loop)
        , deadline_(deadline)
        , state_(std::make_shared<SleepState>())
    {
    }

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

    [[nodiscard]] bool await_ready() const noexcept
    {
        return deadline_ <= Timestamp::clock::now();
    }

    void await_suspend(std::coroutine_handle<> coroutine)
    {
        loop_.assertInLoopThread();
        state_->coroutine = coroutine;
        std::weak_ptr<SleepState> weakState = state_;
        timer_ =
            loop_.runAt(deadline_,
                        [weakState, &loop = loop_]
                        {
                            auto state = weakState.lock();
                            if (!state || !state->coroutine || state->scheduled)
                            {
                                return;
                            }

                            state->scheduled = true;
                            loop.queueInLoop(
                                [state]
                                {
                                    if (!state->coroutine || state->resumed)
                                    {
                                        return;
                                    }
                                    state->resumed = true;
                                    auto suspended = state->coroutine;
                                    state->coroutine = {};
                                    suspended.resume();
                                });
                        });
        timerRegistered_ = true;
    }

    void await_resume() const noexcept {}

private:
    net::EventLoop& loop_;
    Timestamp deadline_;
    std::shared_ptr<SleepState> state_;
    net::TimerId timer_;
    bool timerRegistered_ = false;
};

}  // namespace

Task<void> sleepUntil(net::EventLoop& loop, Timestamp deadline)
{
    co_await SleepAwaiter{loop, deadline};
}

}  // namespace chaoxi::v2
