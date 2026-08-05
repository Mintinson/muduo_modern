#pragma once

#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/v2/Task.hpp"

#include <coroutine>
#include <exception>
#include <functional>
#include <utility>

namespace chaoxi::v2
{

using CoroutineExceptionHandler =
    std::move_only_function<void(std::exception_ptr)>;

namespace detail
{

class DetachedTask
{
public:
    struct promise_type
    {
        DetachedTask get_return_object() noexcept
        {
            return DetachedTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() const noexcept { return {}; }

        std::suspend_never final_suspend() const noexcept { return {}; }

        void return_void() const noexcept {}

        void unhandled_exception() const noexcept { std::terminate(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

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

    [[nodiscard]] handle_type release() noexcept
    {
        return std::exchange(coroutine_, {});
    }

private:
    handle_type coroutine_;
};

inline DetachedTask detach(Task<void> task,
                           CoroutineExceptionHandler exceptionHandler)
{
    try
    {
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

inline void spawn(net::EventLoop& loop,
                  Task<void> task,
                  CoroutineExceptionHandler exceptionHandler = {})
{
    auto detached = detail::detach(std::move(task), std::move(exceptionHandler));
    auto coroutine = detached.release();

    loop.queueInLoop(
        [coroutine]
        {
            if (coroutine && !coroutine.done())
            {
                coroutine.resume();
            }
        });
    loop.wakeup();
}

}  // namespace chaoxi::v2
