#pragma once

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/Channel.hpp"

#include <set>
#include <unordered_set>
#include <utility>

namespace chaoxi::net
{

class EventLoop;
class Timer;
class TimerId;

///
/// A best efforts timer queue.
/// No guarantee that the callback will be on time.
/// TimerQueue 会在构造的时候就创建 timerfd 和 对应的 Channel，
/// 并将该Channel加入到 loop 中的 Poller
///
class TimerQueue
{
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();
    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;
    ///
    /// Schedules the callback to be run at given time,
    /// repeats if @c interval > 0.0.
    ///
    /// Must be thread safe. Usually be called from other threads.
    TimerId addTimer(TimerCallback cb, Timestamp when, double interval);

    void cancel(TimerId timerId);

    [[nodiscard]] int pollTimeoutMs(int defaultTimeoutMs) const noexcept;
    void processExpired();

private:
    // 支持透明比较 (Heterogeneous Lookup) 的比较器
    struct TimerCompare
    {
        using is_transparent = void;  // C++14 开启透明比较的关键标志

        // 辅助结构：统一 unique_ptr 和 裸指针 的视图
        struct Helper
        {
            Timestamp time;
            const Timer* ptr;

            Helper(const std::pair<Timestamp, std::unique_ptr<Timer>>& p)
                : time(p.first)
                , ptr(p.second.get())
            {
            }

            Helper(const std::pair<Timestamp, Timer*>& p)
                : time(p.first)
                , ptr(p.second)
            {
            }
        };

        bool operator()(const Helper& lhs, const Helper& rhs) const
        {
            if (lhs.time < rhs.time)
            {
                return true;
            }
            if (rhs.time < lhs.time)
            {
                return false;
            }
            return lhs.ptr < rhs.ptr;
        }
    };

    // 使用 pair, 来处理两个到期时间相同的 Timer
    using Entry = std::pair<Timestamp, std::unique_ptr<Timer>>;
    using TimerList = std::set<Entry, TimerCompare>;

    // ActiveTimer 只是用于快速查找和校验的非拥有视图，用裸指针即可
    using ActiveTimer = std::pair<Timer*, int64_t>;

    using ActiveTimerSet = std::set<ActiveTimer>;
    // struct ActiveTimerHash
    // {
    //     std::size_t operator()(const ActiveTimer& p) const noexcept
    //     {
    //         // 常见的哈希组合算法 (类似 boost::hash_combine)
    //         auto h1 = std::hash<Timer*>{}(p.first);
    //         auto h2 = std::hash<int64_t>{}(p.second);
    //         return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    //     }
    // };

    // using ActiveTimerSet = std::unordered_set<ActiveTimer, ActiveTimerHash>;

    void addTimerInLoop(std::unique_ptr<Timer> timer);
    void cancelInLoop(TimerId timerId);

    void handleRead();

    // 3. 返回 unique_ptr 集合，转移所有权
    // 这个函数会从timers_中移除已到期的Timer，并通过vector返回它们。
    std::vector<std::unique_ptr<Timer>> getExpired(Timestamp now);
    void reset(std::vector<std::unique_ptr<Timer>>& expired, Timestamp now);

    bool insert(std::unique_ptr<Timer> timer);

    EventLoop* loop_;
#ifndef _WIN32
    const int timerfd_;
    Channel timerfdChannel_;  // 来观察timerfd_上的readable事件
#endif

    TimerList timers_;
    ActiveTimerSet activeTimers_;

    bool callingExpiredTimers_ = false;
    ActiveTimerSet cancelingTimers_;
};
}  // namespace chaoxi::net
