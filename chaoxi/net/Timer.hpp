#pragma once

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Callbacks.hpp"

#include <atomic>
#include <cstdint>
#include <utility>

namespace chaoxi::net
{
class Timer
{
public:
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;
    ~Timer() = default;

    Timer(TimerCallback cb, Timestamp when, double interval)
        : callback_(std::move(cb))
        , expiration_(when)
        , interval_(interval)
        , repeat_(interval > 0.0)
        ,
        // 3. std::atomic 配合 relaxed 内存序，榨干最后一点性能
        // TODO: benchmark 这一部分
        sequence_(s_numCreated_.fetch_add(1, std::memory_order_relaxed) + 1)
    {
    }

    void run() const
    {
        if (callback_)
        {
            callback_();
        }
    }

    [[nodiscard]] Timestamp expiration() const noexcept { return expiration_; }

    [[nodiscard]] bool repeat() const noexcept { return repeat_; }

    [[nodiscard]] int64_t sequence() const noexcept { return sequence_; }

    void restart(Timestamp now) noexcept;

    [[nodiscard]] static int64_t numCreated() noexcept
    {
        return s_numCreated_.load(std::memory_order_relaxed);
    }

private:
    TimerCallback callback_;
    Timestamp expiration_;
    std::chrono::duration<double, std::chrono::seconds::period> interval_;
    const bool repeat_;
    const int64_t sequence_;

    static inline std::atomic<int64_t> s_numCreated_{0};
};
}  // namespace chaoxi::net