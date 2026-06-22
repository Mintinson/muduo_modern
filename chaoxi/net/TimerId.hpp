#pragma once
#include <cstdint>

namespace chaoxi::net
{
class Timer;

/// @brief 用于取消 Timer 的不透明标识符 (Opaque identifier)
class TimerId
{
public:
    //  Rule of Zero
    TimerId() noexcept = default;

    TimerId(Timer* timer, int64_t seq) noexcept : timer_(timer), sequence_(seq)
    {
    }

    friend class TimerQueue;

private:
    Timer* timer_ = nullptr;
    int64_t sequence_ = 0;
};
}  // namespace chaoxi::net