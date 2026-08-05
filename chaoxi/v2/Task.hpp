#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace chaoxi::v2
{

namespace detail
{

struct FinalAwaiter
{
    [[nodiscard]] bool await_ready() const noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<Promise> handle) const noexcept
    {
        auto continuation = handle.promise().continuation_;
        return continuation ? continuation : std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

}  // namespace detail

template <typename T>
class [[nodiscard]] Task
{
public:
    static_assert(!std::is_reference_v<T>,
                  "Task<T&> is intentionally unsupported");

    struct promise_type
    {
        Task get_return_object() noexcept
        {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() const noexcept { return {}; }

        detail::FinalAwaiter final_suspend() const noexcept { return {}; }

        void unhandled_exception() noexcept
        {
            exception_ = std::current_exception();
        }

        template <typename U>
            requires std::constructible_from<T, U&&>
        void return_value(U&& value)
        {
            value_.emplace(std::forward<U>(value));
        }

        std::coroutine_handle<> continuation_{};
        std::exception_ptr exception_;
        std::optional<T> value_;
    };

    using handle_type = std::coroutine_handle<promise_type>;

    Task() noexcept = default;

    Task(Task&& other) noexcept : coroutine_(std::exchange(other.coroutine_, {}))
    {
    }

    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            if (coroutine_)
            {
                coroutine_.destroy();
            }
            coroutine_ = std::exchange(other.coroutine_, {});
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task()
    {
        if (coroutine_)
        {
            coroutine_.destroy();
        }
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return static_cast<bool>(coroutine_);
    }

    class Awaiter
    {
    public:
        explicit Awaiter(handle_type coroutine) noexcept : coroutine_(coroutine)
        {
        }

        Awaiter(Awaiter&& other) noexcept
            : coroutine_(std::exchange(other.coroutine_, {}))
        {
        }

        Awaiter(const Awaiter&) = delete;
        Awaiter& operator=(const Awaiter&) = delete;
        Awaiter& operator=(Awaiter&&) = delete;

        ~Awaiter()
        {
            if (coroutine_)
            {
                coroutine_.destroy();
            }
        }

        [[nodiscard]] bool await_ready() const noexcept
        {
            return !coroutine_ || coroutine_.done();
        }

        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<> continuation) noexcept
        {
            coroutine_.promise().continuation_ = continuation;
            return coroutine_;
        }

        T await_resume()
        {
            if (!coroutine_)
            {
                throw std::logic_error("cannot await an empty Task");
            }
            auto& promise = coroutine_.promise();
            if (promise.exception_)
            {
                std::rethrow_exception(promise.exception_);
            }
            return std::move(*promise.value_);
        }

    private:
        handle_type coroutine_;
    };

    Awaiter operator co_await() &&
    {
        return Awaiter{std::exchange(coroutine_, {})};
    }

private:
    explicit Task(handle_type coroutine) noexcept : coroutine_(coroutine) {}

    handle_type coroutine_{};
};

template <>
class [[nodiscard]] Task<void>
{
public:
    struct promise_type
    {
        Task get_return_object() noexcept
        {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() const noexcept { return {}; }

        detail::FinalAwaiter final_suspend() const noexcept { return {}; }

        void return_void() const noexcept {}

        void unhandled_exception() noexcept
        {
            exception_ = std::current_exception();
        }

        std::coroutine_handle<> continuation_{};
        std::exception_ptr exception_;
    };

    using handle_type = std::coroutine_handle<promise_type>;

    Task() noexcept = default;

    Task(Task&& other) noexcept : coroutine_(std::exchange(other.coroutine_, {}))
    {
    }

    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            if (coroutine_)
            {
                coroutine_.destroy();
            }
            coroutine_ = std::exchange(other.coroutine_, {});
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task()
    {
        if (coroutine_)
        {
            coroutine_.destroy();
        }
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return static_cast<bool>(coroutine_);
    }

    class Awaiter
    {
    public:
        explicit Awaiter(handle_type coroutine) noexcept : coroutine_(coroutine)
        {
        }

        Awaiter(Awaiter&& other) noexcept
            : coroutine_(std::exchange(other.coroutine_, {}))
        {
        }

        Awaiter(const Awaiter&) = delete;
        Awaiter& operator=(const Awaiter&) = delete;
        Awaiter& operator=(Awaiter&&) = delete;

        ~Awaiter()
        {
            if (coroutine_)
            {
                coroutine_.destroy();
            }
        }

        [[nodiscard]] bool await_ready() const noexcept
        {
            return !coroutine_ || coroutine_.done();
        }

        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<> continuation) noexcept
        {
            coroutine_.promise().continuation_ = continuation;
            return coroutine_;
        }

        void await_resume()
        {
            if (!coroutine_)
            {
                throw std::logic_error("cannot await an empty Task");
            }
            const auto& promise = coroutine_.promise();
            if (promise.exception_)
            {
                std::rethrow_exception(promise.exception_);
            }
        }

    private:
        handle_type coroutine_;
    };

    Awaiter operator co_await() &&
    {
        return Awaiter{std::exchange(coroutine_, {})};
    }

private:
    explicit Task(handle_type coroutine) noexcept : coroutine_(coroutine) {}

    handle_type coroutine_{};
};

}  // namespace chaoxi::v2
