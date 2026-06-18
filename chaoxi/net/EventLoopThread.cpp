#include "chaoxi/net/EventLoopThread.hpp"

#include "chaoxi/net/EventLoop.hpp"

#include <utility>

#ifdef OLD

namespace chaoxi::net {

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb, std::string name)
    : callback_(std::move(cb))
    , name_(std::move(name)) {}

EventLoopThread::~EventLoopThread() {
    exiting_ = true;
    // not 100% race-free, eg. threadFunc could be running callback_.
    if (loop_ != nullptr) {
        // still a tiny chance to call destructed object, if threadFunc exits
        // just now. but when EventLoopThread destructs, usually programming is
        // exiting anyway.
        loop_->quit();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

EventLoop* EventLoopThread::startLoop() {
    //   assert(!thread_.started());
    //   thread_.start();
    thread_ = std::thread([this]() { threadFunc(); });
    EventLoop* loop = nullptr;
    {
        std::unique_lock lock(mutex_);
        cond_.wait(lock, [this]() { return loop_ != nullptr; });
        loop = loop_;
    }

    return loop;
}

void EventLoopThread::threadFunc() {
    EventLoop loop;

    if (callback_) {
        callback_(&loop);
    }

    {
        std::scoped_lock lock{mutex_};
        loop_ = &loop;
        cond_.notify_one();
    }

    loop.loop();
    std::scoped_lock lock{mutex_};
    loop_ = nullptr;
}

}  // namespace chaoxi::net

#else
namespace chaoxi::net {

EventLoopThread::EventLoopThread(ThreadInitCallback cb, std::string name)
    : callback_(std::move(cb))
    , name_(std::move(name)) {}

EventLoopThread::~EventLoopThread() {
    EventLoop* loop = nullptr;
    {
        // 修复原版潜在的 Data Race: 析构时读取 loop_ 也必须加锁
        std::scoped_lock lock(mutex_);
        loop = loop_;
    }

    if (loop != nullptr) {
        loop->quit();
        // 注意：这里不需要手动调用 thread_.join()。
        // C++20 的 std::jthread 会在自身析构时自动阻塞等待线程结束，
        // 完美保证了线程安全地退出。
    }
}

EventLoop* EventLoopThread::startLoop() {
    // 1. 使用 std::promise 替代条件变量，极其优雅的异步传值
    std::promise<EventLoop*> promise;
    auto future = promise.get_future();

    // 2. 现代 C++ 线程是在构造时直接启动的，因此用 Lambda 启动并捕获 promise
    thread_ = std::jthread([this, &promise]() { threadFunc(promise); });

    // 3. 阻塞等待子线程将 EventLoop 创建完毕并塞入 promise
    return future.get();
}

void EventLoopThread::threadFunc(std::promise<EventLoop*>& promise) {
    EventLoop loop;  // 栈上分配的 EventLoop

    if (callback_) {
        callback_(&loop);
    }

    {
        std::scoped_lock lock(mutex_);
        loop_ = &loop;
    }

    // 唤醒 startLoop() 中正在 get() 阻塞的主线程
    promise.set_value(&loop);

    // 死循环，直到被调用 quit()
    loop.loop();

    // 线程即将结束，安全地将裸指针置空
    std::scoped_lock lock(mutex_);
    loop_ = nullptr;
}

}  // namespace chaoxi::net
#endif