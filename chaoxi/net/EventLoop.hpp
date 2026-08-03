#pragma once

///
/// @file EventLoop.hpp
/// @brief Reactor 事件循环 —— muduo 网络库的核心调度器
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║                     EventLoop —— "谁有事？我来处理"                    ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║  EventLoop 是整个 muduo Reactor 模式的"引擎":                        ║
/// ║                                                                      ║
/// ║    while (!quit_) {                                                  ║
/// ║        activeChannels_ = poller_->poll(timeoutMs);  // ① 阻塞等事件  ║
/// ║        for (Channel* ch : activeChannels_) {         // ② 分发事件   ║
/// ║            ch->handleEvent(pollReturnTime);                           ║
/// ║        }                                                             ║
/// ║        doPendingFunctors();  // ③ 执行跨线程任务                      ║
/// ║    }                                                                 ║
/// ║                                                                      ║
/// ║  ⚠️ 核心约束：每个线程至多拥有一个 EventLoop（one loop per thread）。 ║
/// ║     ⚠️ 所有 I/O 操作必须在 EventLoop 所在线程执行。                   ║
/// ║     ⚠️ 跨线程操作通过 runInLoop/queueInLoop + wakeupFd 实现。        ║
/// ║                                                                      ║
/// ║  架构图：                                                             ║
/// ║                                                                      ║
/// ║  ┌────────────────── EventLoop ──────────────────────┐                ║
/// ║  │  threadId_    ← 绑定到线程                             │           ║
/// ║  │  poller_      ← I/O 多路复用 (PollPoller/EPollPoller)  │           ║
/// ║  │  timerQueue_  ← 定时器队列（内部用 timerfd）             │           ║
/// ║  │  wakeupFd_    ← eventfd，跨线程唤醒通道                  │           ║
/// ║  │  wakeupChannel_ ← wakeupFd_ 的 Channel 包装             │           ║
/// ║  │  pendingFunctors_ ← 跨线程任务队列                       │           ║
/// ║  │  activeChannels_ ← poll 返回的就绪 Channel 列表          │           ║
/// ║  └────────────────────────────────────────────────────────┘                ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝
///

#include "chaoxi/base/CurrentThread.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/TimerId.hpp"
#include "chaoxi/net/Platform.hpp"

#include <any>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace chaoxi::net {

class Channel;
class Poller;
class TimerQueue;

///
/// @brief Reactor 事件循环 —— 线程绑定的无限循环，驱动所有 I/O 事件
///
/// 每个 EventLoop 绑定一个线程（构造时记录 threadId_），
/// 所有 public 方法（除 runInLoop/queueInLoop/quit 外）都必须在绑定线程调用。
///
/// 跨线程安全的方法：
///   - quit()     : 可从任意线程调用，设置 quit_ 并 wakeup
///   - runInLoop(): 如果在 EventLoop 线程则直接执行，否则 queueInLoop
///   - queueInLoop(): 将任务添加到 pendingFunctors_ 并 wakeup
///   - runAt / runAfter / runEvery / cancel: 通过 runInLoop 委托
///
class EventLoop {
public:
    /// move_only_function: C++23 可移动不可复制的回调，避免分配
    using Functor = std::move_only_function<void()>;

    EventLoop();
    ~EventLoop();  // force out-line dtor, for std::unique_ptr members.
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    ///
    /// @brief 进入事件循环（阻塞！直到 quit() 被调用）
    ///
    /// 循环体：poll 等事件 → 分发事件 → 执行 pending functors
    /// 必须在 EventLoop 所在线程调用。
    ///
    void loop();

    ///
    /// @brief 退出事件循环
    ///
    /// 跨线程安全（内部调用 wakeup() 唤醒 poll）。
    /// 注意：如果在其他线程通过裸指针调用，可能发生 use-after-free，
    /// 推荐通过 shared_ptr<EventLoop> 调用。
    ///
    void quit();

    /// poll 返回的时间戳，通常表示数据到达的时刻
    [[nodiscard]] Timestamp pollReturnTime() const noexcept {
        return pollReturnTime_;
    }

    /// 当前 loop 迭代次数
    [[nodiscard]] int64_t iteration() const noexcept { return iteration_; }

    ///
    /// @brief 在 EventLoop 线程中执行回调
    ///
    /// - 如果在 EventLoop 线程：直接同步执行
    /// - 如果在其他线程：通过 queueInLoop 编入队列 + wakeup
    /// 跨线程安全。
    ///
    void runInLoop(Functor cb);

    ///
    /// @brief 将回调排入 EventLoop 的任务队列
    ///
    /// 在当前 poll 迭代的 doPendingFunctors 阶段执行。
    /// 如果在其他线程调用，或正在执行 pending functors，会 wakeup poll。
    /// 跨线程安全。
    ///
    void queueInLoop(Functor cb);

    /// 当前 pending 任务队列大小
    [[nodiscard]] size_t queueSize() const;

    // ---- 定时器（跨线程安全，内部通过 runInLoop 实现） ----

    /// 在指定时间点执行回调
    TimerId runAt(Timestamp time, TimerCallback cb);
    /// 延迟 delay 秒后执行回调
    TimerId runAfter(double delay, TimerCallback cb);
    /// 每 interval 秒执行一次回调
    TimerId runEvery(double interval, TimerCallback cb);
    /// 取消一个定时器
    void cancel(TimerId timerId);

    // ---- 内部接口（由 Channel/Poller/TimerQueue 调用） ----

    /// 通过 eventfd 唤醒 poll（用于跨线程通知）
    void wakeup();

    /// 更新 Channel 在 Poller 中的事件监听
    void updateChannel(Channel* channel);
    /// 从 Poller 中移除 Channel
    void removeChannel(Channel* channel);
    /// 检查 Channel 是否在 Poller 中
    [[nodiscard]] bool hasChannel(Channel* channel);

    /// 断言当前线程是 EventLoop 绑定线程，否则 abort
    void assertInLoopThread() {
        if (!isInLoopThread()) {
            abortNotInLoopThread();
        }
    }

    /// 判断当前线程是否是 EventLoop 所在线程
    [[nodiscard]] bool isInLoopThread() const noexcept {
        return threadId_ == CurrentThread::tid();
    }

    /// 当前是否正在处理 I/O 事件
    [[nodiscard]] bool eventHandling() const noexcept { return eventHandling_; }

    // ---- 用户上下文（任意类型） ----

    void setContext(const std::any& context) { context_ = context; }
    [[nodiscard]] const std::any& getContext() const { return context_; }
    [[nodiscard]] std::any* getMutableContext() { return &context_; }

    /// @brief 获取当前线程的 EventLoop 指针，没有则返回 nullptr
    [[nodiscard]] static EventLoop* getEventLoopOfCurrentThread();

private:
    void abortNotInLoopThread();
    void handleRead();  // waked up —— 读取 eventfd 的清零操作
    void doPendingFunctors();

    void printActiveChannels() const;  // DEBUG
    using ChannelList = std::vector<Channel*>;

    // ---- 状态标志 ----
    bool looping_{false};                          ///< 是否正在 loop()
    std::atomic<bool> quit_{false};                ///< 是否请求退出
    bool eventHandling_{false};                    ///< 是否正在处理事件
    bool callingPendingFunctors_{false};            ///< 是否正在执行 pending 任务
    int64_t iteration_{0};                         ///< loop 迭代计数
    const int threadId_;                           ///< 绑定的线程 ID（不可变）

    // ---- 核心组件 ----
    Timestamp pollReturnTime_;                                ///< 上次 poll 返回时间
    std::unique_ptr<Poller> poller_;                          ///< I/O 多路复用
    std::unique_ptr<TimerQueue> timerQueue_;                  ///< 定时器队列

    // ---- 跨线程唤醒机制 ----
    SocketHandle wakeupFd_;                    ///< eventfd 或 Windows loopback socket
    std::unique_ptr<Channel> wakeupChannel_;   ///< eventfd 的 Channel 包装

    // ---- 用户数据和中间状态 ----
    std::any context_;                         ///< 用户自定义上下文
    ChannelList activeChannels_;                ///< poll 返回的就绪 Channel 列表
    Channel* currentActiveChannel_{nullptr};    ///< 当前正在分发的 Channel（用于断言）

    // ---- 跨线程任务队列 ----
    mutable std::mutex mutex_;                  ///< 保护 pendingFunctors_
    std::vector<Functor> pendingFunctors_;      ///< @GuardedBy mutex_
};

}  // namespace chaoxi::net
