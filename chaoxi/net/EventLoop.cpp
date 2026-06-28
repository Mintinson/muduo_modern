///
/// @file EventLoop.cpp
/// @brief EventLoop 实现 —— 构造、事件循环、跨线程唤醒、定时器委托
///

#include "chaoxi/net/EventLoop.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/Poller.hpp"
#include "chaoxi/net/SocketOps.hpp"
#include "chaoxi/net/TimerQueue.hpp"

#include <algorithm>
#include <cassert>
#include <csignal>
#include <format>
#include <mutex>

#include <sys/eventfd.h>

#define SECTION 2

#if SECTION == 1
    #include <poll.h>
#endif  // SECTION == 1

namespace chaoxi::net {
namespace {

/// 每个线程独有的 EventLoop 指针（thread_local 确保每线程最多一个）
thread_local EventLoop* t_loopInThisThread = nullptr;

/// poll 超时时间（毫秒）。10 秒后即使没有事件也会醒来检查状态。
/// 这样即使没有 I/O 事件，至少每 10 秒处理一次 pending functors。
constexpr int kPollTimeMs = 10000;

///
/// @brief 创建 eventfd —— 用于跨线程唤醒 poll
///
/// eventfd 是一个内核维护的 64 位计数器 fd：
///   - write: 增加计数器（跨线程唤醒 poll）
///   - read:  读取并重置计数器（清零，使下一次 poll 重新阻塞）
///   - EFD_NONBLOCK: 非阻塞（读空计数器返回 EAGAIN）
///   - EFD_CLOEXEC:  exec 时自动关闭
///
int createEventfd() {
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0) {
        LOG_SYSERR << "Failed in eventfd";
        std::abort();
    }
    return evtfd;
}

/// 忽略 SIGPIPE —— 向已关闭的 socket 写会触发 SIGPIPE，
/// 网络库中应该通过 EPIPE errno 处理，而不是让进程终止。
struct IgnoreSigPipe {
    IgnoreSigPipe() { std::signal(SIGPIPE, SIG_IGN); }
};

IgnoreSigPipe initObj;
}  // namespace

// ============================================================================
// 构造 / 析构
// ============================================================================

/// @brief 获取当前线程的 EventLoop 指针
EventLoop* EventLoop::getEventLoopOfCurrentThread() {
    return t_loopInThisThread;
}

///
/// @brief 构造 EventLoop —— 绑定到当前线程，创建 Poller、TimerQueue、wakeupFd
///
/// 构造顺序：
///   ① 记录当前线程 ID（threadId_ = CurrentThread::tid()）
///   ② 创建 Poller（poll/epoll 实例）
///   ③ 创建 TimerQueue（内部包含 timerfd）
///   ④ 创建 wakeupFd_（eventfd）
///   ⑤ 创建 wakeupChannel_ 包装 eventfd
///   ⑥ 检查 t_loopInThisThread —— 如果已有 EventLoop 则 FATAL
///   ⑦ 设置 wakeupChannel_ 的读回调为 handleRead
///   ⑧ 启用 wakeupChannel_ 的读事件 —— 从此 poll 开始监控 eventfd
///
EventLoop::EventLoop()
    : threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , timerQueue_(std::make_unique<TimerQueue>(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(std::make_unique<Channel>(this, wakeupFd_)) {
    LOG_DEBUG << std::format("EventLoop created {} in thread {}",
                             static_cast<void*>(this), threadId_);

    if (t_loopInThisThread != nullptr) {
        // ❌ 一线一循环：同一线程创建第二个 EventLoop 直接 fatal
        LOG_FATAL << std::format("Another EventLoop {} exists in this thread {}",
                                 static_cast<void*>(t_loopInThisThread),
                                 threadId_);
    } else {
        t_loopInThisThread = this;  // 登记自己
    }

    // eventfd 可读回调：读到 8 字节（清空计数器）
    wakeupChannel_->setReadCallback([this](Timestamp) { handleRead(); });
    // we are always reading the wakeupfd
    wakeupChannel_->enableReading();  //  让 poll 监听 eventfd
}

///
/// @brief 析构 —— 清理资源，释放当前线程的 EventLoop 注册
///
EventLoop::~EventLoop() {
    LOG_DEBUG << std::format("EventLoop {} of thread {} destructs in thread {}",
                             static_cast<void*>(this), threadId_,
                             CurrentThread::tid());
    // 先关闭 Channel 再 close fd
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    // 释放当前线程的注册
    t_loopInThisThread = nullptr;
}

// ============================================================================
// loop —— 核心事件循环
// ============================================================================

#if SECTION == 1
/// 调试/临时版 loop —— 只是一个 sleep 5 秒的占位
void EventLoop::loop() {
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;

    ::poll(NULL, 0, 5 * 1000);  // 让 poller_ 有机会正确初始化

    LOG_TRACE << std::format("EventLoop {} stoping looping",
                             static_cast<void*>(this));

    looping_ = false;
}
#elif SECTION == 2

///
/// @brief 主事件循环 —— 阻塞直到 quit() 被调用
///
/// 每次迭代的 4 个阶段：
///   1. poller_->poll() → 阻塞等待 I/O 事件（最多 kPollTimeMs = 10s）
///   2. 遍历 activeChannels_ → 调用每个 Channel 的 handleEvent
///   3. doPendingFunctors() → 执行跨线程任务
///   4. 检查 quit_ → 继续或退出
///
void EventLoop::loop() {
    assert(!looping_);     // 不能重复调用
    assertInLoopThread();  // 必须在自己的线程

    looping_ = true;
    quit_.store(false, std::memory_order_release);
    LOG_TRACE << "EventLoop " << this << " start looping";

    // ① 循环直到有人调用 quit()
    while (!quit_.load(std::memory_order_acquire)) {
        activeChannels_.clear();

        // ② 阻塞等待 I/O 事件（或超时 10 秒）
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        ++iteration_;

        if (Logger::logLevel() <= Logger::LogLevel::TRACE) {
            printActiveChannels();
        }

        eventHandling_ = true;

        // ③ 遍历所有就绪的 Channel，分发事件
        for (Channel* channel : activeChannels_) {
            currentActiveChannel_ = channel;
            currentActiveChannel_->handleEvent(pollReturnTime_);
        }
        currentActiveChannel_ = nullptr;
        eventHandling_ = false;

        // ④ 执行排队的跨线程任务
        doPendingFunctors();
    }

    LOG_TRACE << "EventLoop " << this << " stop looping";
    looping_ = false;
}
#endif

// ============================================================================
// quit —— 退出 signal
// ============================================================================

///
/// @brief 请求退出事件循环
///
/// 如果不在 EventLoop 线程调用，还需要 wakeup() 来唤醒 poll，
/// 否则 poll 会一直阻塞到超时（10 秒）才检查 quit_。
///
void EventLoop::quit() {
    quit_.store(true, std::memory_order_release);
    if (!isInLoopThread()) {
        wakeup();  // 唤醒 poll，让它立即检查 quit_
    }
}

// ============================================================================
// runInLoop / queueInLoop —— 跨线程任务调度
// ============================================================================

///
/// @brief 在 EventLoop 线程中执行回调
///
/// 快速路径（在当前线程）：直接执行，零开销。
/// 慢速路径（其他线程）：入队 + wakeup。
///
void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

///
/// @brief 将回调排入任务队列
///
/// 如果不在 EventLoop 线程，或正在执行 pending functors（防止递归），
/// 都需要 wakeup 唤醒 poll 来消费新入队的任务。
///
void EventLoop::queueInLoop(Functor cb) {
    {
        std::scoped_lock lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }

    if (!isInLoopThread() || callingPendingFunctors_) {
        wakeup();
    }
}

/// 返回当前 pending 任务数
size_t EventLoop::queueSize() const {
    std::scoped_lock lock(mutex_);
    return pendingFunctors_.size();
}

// ============================================================================
// 定时器 —— 委托给 TimerQueue
// ============================================================================

TimerId EventLoop::runAt(Timestamp time, TimerCallback cb) {
    return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback cb) {
    Timestamp time(addTime(Timestamp::clock::now(), delay));
    return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback cb) {
    Timestamp time(addTime(Timestamp::clock::now(), interval));
    return timerQueue_->addTimer(std::move(cb), time, interval);
}

void EventLoop::cancel(TimerId timerId) {
    timerQueue_->cancel(timerId);
}

// ============================================================================
// Channel / Poller 管理
// ============================================================================

void EventLoop::updateChannel(Channel* channel) {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    if (eventHandling_) {
        // 确保不会在事件分发中移除正在处理的 Channel（会破坏遍历）
        assert(currentActiveChannel_ == channel ||
               std::find(activeChannels_.begin(), activeChannels_.end(),
                         channel) == activeChannels_.end());
    }
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel) {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    return poller_->hasChannel(channel);
}

// ============================================================================
// abortNotInLoopThread —— 线程断言失败处理
// ============================================================================

void EventLoop::abortNotInLoopThread() {
    LOG_FATAL << std::format(
        "EventLoop::abortNotInLoopThread - EventLoop {} was created in "
        "threadId_ = {}, current thread id = {}",
        static_cast<void*>(this), threadId_, CurrentThread::tid());
}

// ============================================================================
// wakeup / handleRead —— 跨线程唤醒机制
// ============================================================================

///
/// @brief 向 eventfd 写入 8 字节，唤醒 poll
///
/// 跨线程调用：其他线程调用了 queueInLoop 后，
/// 需要让 EventLoop 线程从 poll 中醒来处理新任务。
///
void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one) {
        LOG_ERROR << std::format(
            "EventLoop::wakeup() writes {} bytes instead of 8", n);
    }
}

///
/// @brief 读取 eventfd 清零 —— 处理唤醒事件
///
/// 当 eventfd 可读时被 wakeupChannel_ 的读回调调用。
/// eventfd 被 read 后计数器归零，poll 重新阻塞。
///
void EventLoop::handleRead() {
    uint64_t one = 1;
    ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one) {
        LOG_ERROR << std::format(
            "EventLoop::handleRead() reads {} bytes instead of 8", n);
    }
}

// ============================================================================
// doPendingFunctors —— 执行跨线程任务
// ============================================================================

///
/// @brief 执行所有排队的跨线程任务
///
/// 设计细节：
///   - 用 swap 将任务从 pendingFunctors_ 移动到局部变量，
///     减少临界区长度（不阻塞其他线程的 queueInLoop）。
///   - 同时也避免了死锁：因为 Functor 中可能再次调用 queueInLoop。
///
void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::scoped_lock lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    std::ranges::for_each(functors, [](auto& functor) { functor(); });
    callingPendingFunctors_ = false;
}

// ============================================================================
// DEBUG
// ============================================================================

void EventLoop::printActiveChannels() const {
    std::ranges::for_each(activeChannels_, [](const Channel* channel) {
        LOG_TRACE << "{" << channel->reventsToString() << "} ";
    });
}

}  // namespace chaoxi::net
