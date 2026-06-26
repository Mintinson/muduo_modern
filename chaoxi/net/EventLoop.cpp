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

namespace chaoxi::net
{
namespace
{

thread_local EventLoop* t_loopInThisThread = nullptr;
constexpr int kPollTimeMs = 10000;

int createEventfd()
{
    // 用于创建一个事件通知文件描述符，当事件发生时可以通过该文件描述符进行通知
    // 初始化计数器值为 0。后续可以通过 write增加计数，read读取并重置。
    // FD_NONBLOCK：设置非阻塞模式。read操作在计数器为 0 时不会阻塞，而是返回
    // EAGAIN。 EFD_CLOEXEC：在执行
    // exec族函数时自动关闭此文件描述符，防止子进程继承。
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_SYSERR << "Failed in eventfd";
        std::abort();
    }
    return evtfd;
}

struct IgnoreSigPipe
{
    IgnoreSigPipe() { std::signal(SIGPIPE, SIG_IGN); }
};

IgnoreSigPipe initObj;
}  // namespace

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
    return t_loopInThisThread;
}

EventLoop::EventLoop()
    : threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , timerQueue_(std::make_unique<TimerQueue>(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(std::make_unique<Channel>(this, wakeupFd_))
{
    LOG_DEBUG << std::format("EventLoop created {} in thread {}",
                             static_cast<void*>(this), threadId_);

    if (t_loopInThisThread != nullptr)
    {
        LOG_FATAL << std::format("Another EventLoop {} exists in this thread {}",
                                 static_cast<void*>(t_loopInThisThread),
                                 threadId_);
    }
    else
    {
        t_loopInThisThread = this;  // 登记自己
    }
    wakeupChannel_->setReadCallback([this](Timestamp) { handleRead(); });
    // we are always reading the wakeupfd
    wakeupChannel_->enableReading();  //  让 poll 监听 eventfd
}

EventLoop::~EventLoop()
{
    LOG_DEBUG << std::format("EventLoop {} of thread {} destructs in thread {}",
                             static_cast<void*>(this), threadId_,
                             CurrentThread::tid());
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// void EventLoop::loop() {
//     assert(!looping_);
//     assertInLoopThread();
//     looping_ = true;
//     quit_.store(false, std::memory_order_release);
//     LOG_TRACE << "EventLoop " << this << " start looping";

//     while (!quit_.load(std::memory_order_acquire)) {
//         activeChannels_.clear();
//         pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
//         ++iteration_;

//         if (Logger::logLevel() <= Logger::LogLevel::TRACE) {
//             printActiveChannels();
//         }

//         eventHandling_ = true;
//         for (Channel* channel : activeChannels_) {
//             currentActiveChannel_ = channel;
//             currentActiveChannel_->handleEvent(pollReturnTime_);
//         }
//         currentActiveChannel_ = nullptr;
//         eventHandling_ = false;

//         doPendingFunctors();
//     }

//     LOG_TRACE << "EventLoop " << this << " stop looping";
//     looping_ = false;
// }
#if SECTION == 1
void EventLoop::loop()
{
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;

    ::poll(NULL, 0, 5 * 1000);  // 让 poller_ 有机会正确初始化

    LOG_TRACE << std::format("EventLoop {} stoping looping",
                             static_cast<void*>(this));

    looping_ = false;
}
#elif SECTION == 2

void EventLoop::loop()
{
    assert(!looping_);     // 不能重复调用
    assertInLoopThread();  // 必须在自己的线程

    looping_ = true;
    quit_.store(false, std::memory_order_release);
    LOG_TRACE << "EventLoop " << this << " start looping";

    // ① 循环直到有人调用 quit()
    while (!quit_.load(std::memory_order_acquire))
    {
        activeChannels_.clear();

        // ② 阻塞等待 I/O 事件（或超时 10 秒）
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        ++iteration_;

        if (Logger::logLevel() <= Logger::LogLevel::TRACE)
        {
            printActiveChannels();
        }

        eventHandling_ = true;

        // ③ 遍历所有就绪的 Channel，分发事件
        for (Channel* channel : activeChannels_)
        {
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

void EventLoop::quit()
{
    quit_.store(true, std::memory_order_release);
    if (!isInLoopThread())
    {
        wakeup();
    }
}

void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())
    {
        cb();
    }
    else
    {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb)
{
    {
        std::scoped_lock lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }

    if (!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();
    }
}

size_t EventLoop::queueSize() const
{
    std::scoped_lock lock(mutex_);
    return pendingFunctors_.size();
}

TimerId EventLoop::runAt(Timestamp time, TimerCallback cb)
{
    return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback cb)
{
    Timestamp time(addTime(Timestamp::clock::now(), delay));
    // std::println("current is {}, the time is {}", Timestamp::clock::now(),
    // time);

    // auto time = Timestamp::clock::now() +  interval;
    return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback cb)
{
    // Timestamp time(addTime(Timestamp::now(), interval));
    Timestamp time(addTime(Timestamp::clock::now(), interval));
    // std::println("the time is {}")
    return timerQueue_->addTimer(std::move(cb), time, interval);
}

void EventLoop::cancel(TimerId timerId)
{
    timerQueue_->cancel(timerId);
}

void EventLoop::updateChannel(Channel* channel)
{
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    if (eventHandling_)
    {
        assert(currentActiveChannel_ == channel ||
               std::find(activeChannels_.begin(), activeChannels_.end(),
                         channel) == activeChannels_.end());
    }
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread()
{
    LOG_FATAL << std::format(
        "EventLoop::abortNotInLoopThread - EventLoop {} was created in "
        "threadId_ = {}, current thread id = {}",
        static_cast<void*>(this), threadId_, CurrentThread::tid());
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR << std::format(
            "EventLoop::wakeup() writes {} bytes instead of 8", n);
    }
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR << std::format(
            "EventLoop::handleRead() reads {} bytes instead of 8", n);
    }
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    // 回调列表swap()到局部变量functors中，
    // 这样一方面减小了临界区的长度（意味着不会阻塞其他线程调用queueInLoop()），
    // 另一方面也避免了死锁（因为Functor可能再调用queueInLoop()）。
    {
        std::scoped_lock lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    std::ranges::for_each(functors, [](auto& functor) { functor(); });
    callingPendingFunctors_ = false;
}

void EventLoop::printActiveChannels() const
{
    std::ranges::for_each(
        activeChannels_, [](const Channel* channel)
        { LOG_TRACE << "{" << channel->reventsToString() << "} "; });
}

}  // namespace chaoxi::net