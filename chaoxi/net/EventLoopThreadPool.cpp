///
/// @file EventLoopThreadPool.cpp
/// @brief EventLoopThreadPool 实现 —— IO 线程池的启动、分发逻辑
///

#include "chaoxi/net/EventLoopThreadPool.hpp"

#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThread.hpp"

#include <cassert>
#include <format>
#include <memory>

namespace chaoxi::net
{

///
/// @brief 构造 IO 线程池
///
/// @param baseLoop 主 Reactor（acceptor 所在 EventLoop）
/// @param nameArg  线程池名称
///
/// 此时线程池尚未启动（started_ = false），没有创建任何 io 线程。
/// 需要调用 setThreadNum() + start() 来启动。
///
EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop,
                                         std::string nameArg)
    : baseLoop_(baseLoop)
    , name_(std::move(nameArg))
{
}

///
/// @brief 析构 —— 注意：不负责 delete loops_ 中的 EventLoop*
///
/// EventLoop 在 EventLoopThread 内的栈上分配，由 EventLoopThread 的析构
/// 负责清理（先 quit 再 join jthread）。这里的 loops_ 仅保存裸指针用于
/// 快速索引，不拥有所有权。
///
EventLoopThreadPool::~EventLoopThreadPool()
{
    // Don't delete loop, it's stack variable in EventLoopThread
}

///
/// @brief 启动线程池 —— 创建所有 io 线程并让它们各自的 EventLoop 开始循环
///
/// 必须在 baseLoop_ 的线程中调用（由 TcpServer::start() 触发）。
///
/// 执行流程：
///   1. 断言未启动过
///   2. 创建 numThreads_ 个 EventLoopThread 对象
///      - 每个 EventLoopThread 内部创建一个 EventLoop（栈上）
///      - 线程启动后立即调用 loop.loop() 进入事件循环
///   3. 收集每个线程的 EventLoop* 到 loops_ 数组中
///   4. 如果 numThreads_ == 0 但提供了回调，直接在 baseLoop_ 上执行回调
///
/// @param cb 每个 io 线程启动后执行的初始化回调
///
void EventLoopThreadPool::start(const ThreadInitCallback& cb)
{
    assert(!started_);
    baseLoop_->assertInLoopThread();

    started_ = true;

    // 创建 N 个 io 线程
    threads_.reserve(numThreads_);
    loops_.reserve(numThreads_);
    for (int i = 0; i < numThreads_; ++i)
    {
        // EventLoopThread 构造时接收一个回调和一个名称
        // 线程名形如 "serverName0", "serverName1", ...
        threads_.emplace_back(std::make_unique<EventLoopThread>(
            cb, std::format("{}{}", name_, i)));

        // startLoop() 返回子线程中创建的 EventLoop 的指针
        // 内部通过 std::promise/std::future 同步等待，确保 loop 已创建才返回
        loops_.push_back(threads_.back()->startLoop());
    }

    // 特殊情况：numThreads_ == 0 —— 不创建额外线程
    // 直接在 baseLoop_（acceptor 所在线程）上执行回调
    if (numThreads_ == 0 && cb)
    {
        cb(baseLoop_);
    }
}

///
/// @brief round-robin 轮询获取下一个 io loop
///
/// 每次调用返回 loops_[next_ % N]，然后将 next_ 推进一位。
/// 这样保证连接被均匀分配到所有 io 线程。
///
/// 如果 numThreads_ == 0（没创建额外线程），直接返回 baseLoop_。
///
EventLoop* EventLoopThreadPool::getNextLoop() noexcept
{
    baseLoop_->assertInLoopThread();
    assert(started_);
    EventLoop* loop = baseLoop_;

    if (!loops_.empty())
    {
        // round-robin：loops_[0] → loops_[1] → ... → loops_[N-1] → loops_[0]
        loop = loops_[next_];
        ++next_;
        if (static_cast<size_t>(next_) >= loops_.size())
        {
            next_ = 0;  // 回到开头
        }
    }
    return loop;
}

///
/// @brief 基于 hash 的 IO 线程选择
///
/// 与 round-robin 不同，此方法保证相同的 hashCode 始终返回同一个 io loop。
/// 适用场景：需要将同一个客户端的多个连接分配到同一线程处理。
///
/// 实现：hashCode % N
///
EventLoop* EventLoopThreadPool::getLoopForHash(size_t hashCode) noexcept
{
    baseLoop_->assertInLoopThread();
    EventLoop* loop = baseLoop_;

    if (!loops_.empty())
    {
        loop = loops_[hashCode % loops_.size()];
    }
    return loop;
}

///
/// @brief 获取所有 io loop
///
/// 如果 numThreads_ == 0，返回只包含 baseLoop_ 的 span。
/// 否则返回 loops_ 的 span。
///
std::span<EventLoop*> EventLoopThreadPool::getAllLoops()
{
    baseLoop_->assertInLoopThread();
    assert(started_);
    if (loops_.empty())
    {
        return {&baseLoop_, 1};
    }

    return loops_;
}

}  // namespace chaoxi::net
