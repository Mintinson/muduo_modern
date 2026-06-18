#pragma once

#include "chaoxi/base/CurrentThread.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/TimerId.hpp"

#include <any>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace chaoxi::net
{

class Channel;
class Poller;
class TimerQueue;

class EventLoop
{
public:
    using Functor = std::move_only_function<void()>;
    EventLoop();
    ~EventLoop();  // force out-line dtor, for std::unique_ptr members.
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    ///
    /// Loops forever.
    ///
    /// Must be called in the same thread as creation of the object.
    ///
    void loop();

    /// Quits loop.
    ///
    /// This is not 100% thread safe, if you call through a raw pointer,
    /// better to call through shared_ptr<EventLoop> for 100% safety.
    void quit();

    ///
    /// Time when poll returns, usually means data arrival.
    ///
    [[nodiscard]] Timestamp pollReturnTime() const noexcept
    {
        return pollReturnTime_;
    }

    [[nodiscard]] int64_t iteration() const noexcept { return iteration_; }

    /// Runs callback immediately in the loop thread.
    /// It wakes up the loop, and run the cb.
    /// If in the same loop thread, cb is run within the function.
    /// Safe to call from other threads.
    void runInLoop(Functor cb);
    /// Queues callback in the loop thread.
    /// Runs after finish pooling.
    /// Safe to call from other threads.
    void queueInLoop(Functor cb);

    [[nodiscard]] size_t queueSize() const;

    // timers

    ///
    /// Runs callback at 'time'.
    /// Safe to call from other threads.
    ///
    TimerId runAt(Timestamp time, TimerCallback cb);
    ///
    /// Runs callback after @c delay seconds.
    /// Safe to call from other threads.
    ///
    TimerId runAfter(double delay, TimerCallback cb);
    ///
    /// Runs callback every @c interval seconds.
    /// Safe to call from other threads.
    ///
    TimerId runEvery(double interval, TimerCallback cb);
    ///
    /// Cancels the timer.
    /// Safe to call from other threads.
    ///
    void cancel(TimerId timerId);

    // internal usage
    void wakeup();
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    [[nodiscard]] bool hasChannel(Channel* channel);

    // pid_t threadId() const { return threadId_; }
    void assertInLoopThread()
    {
        if (!isInLoopThread())
        {
            abortNotInLoopThread();
        }
    }

    /// @brief 判断当前线程是否是 EventLoop 所在线程
    [[nodiscard]] bool isInLoopThread() const noexcept
    {
        return threadId_ == CurrentThread::tid();
    }

    // bool callingPendingFunctors() const { return callingPendingFunctors_; }
    [[nodiscard]] bool eventHandling() const noexcept { return eventHandling_; }

    void setContext(const std::any& context) { context_ = context; }

    [[nodiscard]] const std::any& getContext() const { return context_; }

    [[nodiscard]] std::any* getMutableContext() { return &context_; }

    /// @brief 每个线程最多只能有一个 EventLoop
    /// 对象，getEventLoopOfCurrentThread() 提供了访问接口
    [[nodiscard]] static EventLoop* getEventLoopOfCurrentThread();

private:
    void abortNotInLoopThread();
    void handleRead();  // waked up
    void doPendingFunctors();

    void printActiveChannels() const;  // DEBUG
    using ChannelList = std::vector<Channel*>;

    bool looping_{false};
    std::atomic<bool> quit_{false};
    bool eventHandling_{false};           // atomic
    bool callingPendingFunctors_{false};  // atomic
    int64_t iteration_{0};
    const int threadId_;
    Timestamp pollReturnTime_;
    std::unique_ptr<Poller> poller_;
    std::unique_ptr<TimerQueue> timerQueue_;
    int wakeupFd_;
    // unlike in TimerQueue, which is an internal class,
    // we don't expose Channel to client.
    std::unique_ptr<Channel> wakeupChannel_;
    std::any context_;
    // scratch variables
    ChannelList activeChannels_;
    Channel* currentActiveChannel_{nullptr};

    mutable std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;  // @GuardedBy mutex_
};

}  // namespace chaoxi::net