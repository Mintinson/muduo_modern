#include "chaoxi/net/TimerQueue.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/Timer.hpp"
#include "chaoxi/net/TimerId.hpp"

#include <cassert>
#include <cstring>
#include <format>

#include <sys/timerfd.h>
#include <unistd.h>

namespace chaoxi::net
{
namespace detail
{
int createTimerfd()
{
    int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd < 0)
    {
        LOG_SYSFATAL << "Failed in timerfd_create";
    }
    return timerfd;
}

struct timespec howMuchTimeFromNow(Timestamp when)
{
    int64_t microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(
            when.time_since_epoch() - Timestamp::clock::now().time_since_epoch())
            .count();
    if (microseconds < 100)
    {
        microseconds = 100;
    }
    struct timespec ts{};
    ts.tv_sec = static_cast<time_t>(microseconds /
                                    std::chrono::microseconds::period::den);
    ts.tv_nsec = static_cast<long>(
        (microseconds % std::chrono::microseconds::period::den) * 1000);
    return ts;
}

void resetTimerfd(int timerfd, Timestamp expiration)
{
    struct itimerspec newValue{};
    struct itimerspec oldValue{};
    newValue.it_value = howMuchTimeFromNow(expiration);
    if (::timerfd_settime(timerfd, 0, &newValue, &oldValue))
    {
        LOG_SYSERR << "timerfd_settime()";
    }
}

void readTimerfd(int timerfd, Timestamp now)
{
    uint64_t howmany;
    ssize_t n = ::read(timerfd, &howmany, sizeof(howmany));
    LOG_TRACE << std::format("TimerQueue::handleRead() {} at {}", howmany, now);
    if (n != sizeof(howmany))
    {
        LOG_ERROR << std::format(
            "TimerQueue::handleRead() reads {} bytes instead of 8", n);
    }
}

}  // namespace detail

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop)
    , timerfd_(detail::createTimerfd())
    , timerfdChannel_(loop, timerfd_)
{
    timerfdChannel_.setReadCallback([this](Timestamp) { handleRead(); });
    timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
    timerfdChannel_.disableAll();
    timerfdChannel_.remove();
    ::close(timerfd_);
    // 由于 timers_ 里面装的是 unique_ptr，
    // 这里不需要再手写循环 delete timer.second 了。资源会自动释放。
}

TimerId TimerQueue::addTimer(TimerCallback cb, Timestamp when, double interval)
{
    auto timer = std::make_unique<Timer>(std::move(cb), when, interval);
    Timer* raw_timer = timer.get();  // 记录裸指针用于生成 TimerId
    TimerId timerId(raw_timer, raw_timer->sequence());

    loop_->runInLoop([this, t = std::move(timer)]() mutable
                     { addTimerInLoop(std::move(t)); });

    return timerId;
}

void TimerQueue::cancel(TimerId timerId)
{
    loop_->runInLoop([this, timerId] { cancelInLoop(timerId); });
}

void TimerQueue::addTimerInLoop(std::unique_ptr<Timer> timer)
{
    loop_->assertInLoopThread();
    Timer* raw = timer.get();
    bool earliestChanged = insert(std::move(timer));

    if (earliestChanged)
    {
        detail::resetTimerfd(timerfd_, raw->expiration());
    }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
    loop_->assertInLoopThread();
    ActiveTimer timer(timerId.timer_, timerId.sequence_);
    auto it = activeTimers_.find(timer);

    if (it != activeTimers_.end())
    {
        // C++14 透明比较发威：直接用 pair<Timestamp, Timer*> 去
        // set<unique_ptr> 中 erase！
        std::pair<Timestamp, Timer*> key(it->first->expiration(), it->first);
        // size_t n = timers_.erase(key);
        // assert(n == 1);
        timers_.erase(key);

        activeTimers_.erase(it);
        // 原版的 delete it->first; 被彻底干掉！unique_ptr 会自动析构它。
    }
    else if (callingExpiredTimers_)
    {  // 应对自注销
        cancelingTimers_.insert(timer);
    }
}

void TimerQueue::handleRead()
{
    loop_->assertInLoopThread();
    Timestamp now = Timestamp::clock::now();
    detail::readTimerfd(timerfd_,
                        now);  // ① 把 timerfd 里的"到期次数"读掉（清零）

    std::vector<std::unique_ptr<Timer>> expired =
        getExpired(now);  // ② 找出所有到期的 Timer

    callingExpiredTimers_ = true;
    cancelingTimers_.clear();
    for (const auto& timer_up : expired)
    {
        timer_up->run();  // ③ 调用 Timer::run() → callback_()
    }
    callingExpiredTimers_ = false;

    reset(expired, now);  // ④ 处理重复定时器，重置下一轮 timerfd
}

std::vector<std::unique_ptr<Timer>> TimerQueue::getExpired(Timestamp now)
{
    std::vector<std::unique_ptr<Timer>> expired;

    // C++17 std::set::extract 终结了极其丑陋的 UINTPTR_MAX 哨兵
    auto it = timers_.begin();
    while (it != timers_.end() && it->first <= now)
    {
        Timer* raw = it->second.get();
        activeTimers_.erase({raw, raw->sequence()});

        // extract 将节点直接从 set
        // 中拔出，不涉及任何内存拷贝/析构，然后转移所有权
        auto node = timers_.extract(it++);
        expired.push_back(std::move(node.value().second));
    }

    return expired;
}

void TimerQueue::reset(std::vector<std::unique_ptr<Timer>>& expired,
                       Timestamp now)
{
    Timestamp nextExpire;

    for (auto& timer_up : expired)
    {
        Timer* raw = timer_up.get();
        ActiveTimer active(raw, raw->sequence());

        // 8. C++20 contains 语法糖
        if (raw->repeat() && !cancelingTimers_.contains(active))
        {
            raw->restart(now);
            insert(std::move(timer_up));
        }
        // 如果不满足条件，timer_up 会在此次循环结束时自然失效，自动 delete！
        // 原版的 delete it.second; 彻底退出历史舞台！
    }

    if (!timers_.empty())
    {
        nextExpire = timers_.begin()->first;
    }

    if (nextExpire.time_since_epoch().count() > 0)
    {
        detail::resetTimerfd(timerfd_, nextExpire);
    }
}

bool TimerQueue::insert(std::unique_ptr<Timer> timer)
{
    loop_->assertInLoopThread();
    bool earliestChanged = false;
    Timestamp when = timer->expiration();
    Timer* raw = timer.get();

    auto it = timers_.begin();
    if (it == timers_.end() || when < it->first)
    {
        earliestChanged = true;
    }

    timers_.insert({when, std::move(timer)});
    activeTimers_.insert({raw, raw->sequence()});

    return earliestChanged;
}

}  // namespace chaoxi::net