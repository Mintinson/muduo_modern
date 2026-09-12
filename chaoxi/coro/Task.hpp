#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

// 参考：https://zhuanlan.zhihu.com/p/2026976153496242090

namespace chaoxi::coro
{

namespace detail
{

struct FinalAwaiter
{
    // 必须返回 false，让协程在 final_suspend 处挂起，保留协程帧，直到 Awaiter
    // 读取完结果并销毁它。
    [[nodiscard]] bool await_ready() const noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<Promise> handle) const noexcept
    {
        auto continuation = handle.promise().continuation_;
        // 当前协程挂起，控制权直接转移到返回的句柄，不会递归调用
        // resume()，避免栈溢出。
        return continuation ? continuation : std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

}  // namespace detail

// 结束时的对称转移
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
                std::coroutine_handle<promise_type>::from_promise(*this),
            };
        }

        // 协程帧创建后立即挂起，不主动执行协程体代码
        [[nodiscard]] std::suspend_always initial_suspend() const noexcept
        {
            return {};
        }

        // 在 promise_type 中指定收尾行为：
        [[nodiscard]] detail::FinalAwaiter final_suspend() const noexcept
        {
            // 通过让 final_suspend 返回一个包含父协程句柄的
            // Awaiter，编译器会采用类似尾调用优化（Tail
            // Call）的机制：

            // 它会首先将当前子协程的物理栈帧安全剥离，然后再以平级跳转的方式进入父协程。

            // 在这种机制的保障下，无论业务逻辑中 co_await
            // 嵌套了多少层，底层的线程调用栈深度始终保持恒定 (O(1))。
            return {};
        }

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

        T result()
        {
            if (exception_)
            {
                std::rethrow_exception(exception_);
            }
            return std::move(value_).value();
        }

        std::coroutine_handle<> continuation_{};
        std::exception_ptr exception_;  // 未捕获的异常
        std::optional<T> value_;        // 产生的值
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

        // 探测状态。询问异步操作是否已经完成。如果返回
        // true，编译器将走“快速通道”，直接跳过挂起阶段；如果返回
        // false，则准备挂起当前协程。
        [[nodiscard]] bool await_ready() const noexcept
        {
            // 如果子协程尚未执行完毕，则强制父协程挂起
            return !coroutine_ || coroutine_.done();
        }

        // 核心拦截点。在当前协程的物理状态（寄存器、局部变量）被安全保存到堆上的协程帧后，
        // 编译器会调用此方法，并将当前（父）协程的句柄作为参数传入。
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<> continuation) noexcept
        {
            // 将父协程的句柄 (next) 记录在子协程的 promise 状态中
            coroutine_.promise().continuation_ = continuation;
            // 返回子协程的句柄，指示 C++ 运行时将执行流切换至子协程
            return coroutine_;
        }

        // 结果提取点。当协程被再次唤醒时，此方法的返回值将作为整个 co_await
        // 表达式的结果。
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
        handle_type coroutine_;  // 子协程的句柄
    };

    // 限制为右值调用，且不转移 handle_ 的所有权
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

        [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }

        [[nodiscard]] detail::FinalAwaiter final_suspend() const noexcept { return {}; }

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

}  // namespace chaoxi::coro
