#pragma once

#include <functional>
#include <mutex>
#include <thread>

// #define OLD

#ifdef OLD
#include <condition_variable>
namespace chaoxi::net {
class EventLoop;

class EventLoopThread {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThread(const ThreadInitCallback& cb = {}, std::string name = "");
    ~EventLoopThread();
    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    EventLoop* startLoop();

private:
    void threadFunc();

    EventLoop* loop_{nullptr};

    bool exiting_{false};
    std::thread thread_{};
    std::mutex mutex_;
    std::condition_variable cond_;
    ThreadInitCallback callback_;
    std::string name_;
};
}  // namespace chaoxi::net
#else
    #include <future>

namespace chaoxi::net {

class EventLoop;

class EventLoopThread {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    explicit EventLoopThread(ThreadInitCallback cb = {}, std::string name = "");
    ~EventLoopThread();

    // 禁用拷贝和移动语义
    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    EventLoop* startLoop();

private:
    // 传入 std::promise 的引用，用于向主线程传递 EventLoop 指针
    void threadFunc(std::promise<EventLoop*>& promise);

    EventLoop* loop_ = nullptr;
    std::jthread thread_;  // C++20 自带自动 join 的线程类
    std::mutex mutex_;     // 仅用于保护生命周期结束时的 loop_ 访问
    ThreadInitCallback callback_;
    std::string name_;
};

}  // namespace chaoxi::net
#endif