#pragma once

#include "chaoxi/net/EventLoop.hpp"

#include <coroutine>

namespace chaoxi::v2
{

class ScheduleOn
{
public:
    explicit ScheduleOn(net::EventLoop& loop) noexcept : loop_(loop) {}

    [[nodiscard]] bool await_ready() const noexcept
    {
        return loop_.isInLoopThread();
    }

    void await_suspend(std::coroutine_handle<> coroutine) const
    {
        loop_.queueInLoop([coroutine] { coroutine.resume(); });
        loop_.wakeup();
    }

    void await_resume() const noexcept {}

private:
    net::EventLoop& loop_;
};

class YieldToEventLoop
{
public:
    explicit YieldToEventLoop(net::EventLoop& loop) noexcept : loop_(loop) {}

    [[nodiscard]] bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> coroutine) const
    {
        loop_.queueInLoop([coroutine] { coroutine.resume(); });
    }

    void await_resume() const noexcept {}

private:
    net::EventLoop& loop_;
};

[[nodiscard]] inline ScheduleOn scheduleOn(net::EventLoop& loop) noexcept
{
    return ScheduleOn{loop};
}

[[nodiscard]] inline YieldToEventLoop yield(net::EventLoop& loop) noexcept
{
    return YieldToEventLoop{loop};
}

}  // namespace chaoxi::v2
