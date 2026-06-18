///
/// @file s00_test1.cpp
/// @brief muduo 最小示例：用 timerfd + Channel + EventLoop 实现一个定时器
///
/// 本示例展示 muduo 网络库最核心的三个组件是如何协作的：
///   - EventLoop:  事件循环（Reactor 模式的核心），负责循环调用 poll，
///                 收集就绪事件，分发给各个 Channel
///   - Channel:    I/O 事件分发器，封装一个 fd 及其感兴趣的事件（读/写），
///                 当 poll 检测到该 fd 就绪时，Channel 负责回调用户逻辑
///   - Poller:     I/O 多路复用的封装（底层是 ::poll 或 ::epoll），
///                 EventLoop 通过它获取当前有哪些 fd 就绪
///
/// 工作流程概览:
///   1. 创建 EventLoop（栈对象）
///   2. 通过 Linux timerfd 机制创建一个定时器 fd
///   3. 把 timerfd 包装成 Channel，注册到 EventLoop 中
///   4. 设置定时器超时时间为 3 秒
///   5. 调用 EventLoop::loop() 进入事件循环
///   6. 3 秒后 timerfd 变为可读，poll 返回 → Channel 回调 timeout()
///   7. timeout() 调用 quit() 退出事件循环
///   8. 清理资源（Channel 从 poller 移除、关闭 fd、析构 EventLoop）
///
/// 资源管理要点（C++ RAII）:
///   - EventLoop 和 Channel 都在栈上，析构顺序是 Channel 先、EventLoop 后
///   - Channel 析构时会断言 addedToLoop_ == false
///   - 因此 loop.loop() 返回后，必须先 disableAll() + remove() 把 Channel
///     从 poller 中摘除，再让 Channel 自然析构，否则断言失败 → core dump
///

#include "chaoxi/base/Timestamp.hpp"   // Timestamp = std::chrono::system_clock::time_point
#include "chaoxi/net/Channel.hpp"      // Channel: fd + 事件回调 的封装
#include "chaoxi/net/EventLoop.hpp"    // EventLoop: Reactor 事件循环

#include <cstddef>
#include <ctime>
#include <print>

#include <strings.h>       // bzero
#include <sys/time.h>      // itimerspec
#include <sys/timerfd.h>   // timerfd_create / timerfd_settime
#include <unistd.h>        // close

///
/// 全局指针，用于在 timeout() 回调中访问 EventLoop，调用 quit()
///
/// 因为 timerfd 的读回调签名是 void(Timestamp)，无法传递 EventLoop*，
/// 所以这里用全局变量作为简易解法。生产代码中通常用 std::bind 或 lambda 捕获。
///
chaoxi::net::EventLoop* g_loop;

///
/// @brief timerfd 超时回调 —— 当定时器到期时被 Channel 调用
///
/// 调用链:
///   poll 返回 timerfd 可读
///   → EventLoop 遍历 activeChannels，调用 Channel::handleEvent(pollReturnTime)
///   → Channel::handleEvent 检测到 POLLIN，调用 readCallback_(receiveTime)
///   → 即此处的 timeout()
///
/// @param ts  poll 返回的时刻（Timestamp），此处未使用，仅符合回调签名
///
void timeout([[maybe_unused]] chaoxi::Timestamp ts) {
    std::println("Timneout!");
    // 通知 EventLoop 退出事件循环，loop.loop() 将在当前迭代结束后返回
    g_loop->quit();
}

int main() {
    // ========================================================================
    // 1. 创建 EventLoop
    // ========================================================================
    // EventLoop 构造函数做了几件关键的事：
    //   - 记录当前线程 ID（之后所有操作都必须在同一线程，这是 Reactor 的线程安全模型）
    //   - 创建 Poller（底层 poll/epoll 实例）
    //   - 创建 TimerQueue（定时器队列，本示例未使用，这里用 timerfd 代替）
    //   - 创建 wakeupFd_（eventfd），用于跨线程唤醒事件循环
    //   - 创建 wakeupChannel_ 并注册读事件（监控 eventfd）
    //     每线程最多一个 EventLoop，通过 thread_local 变量 t_loopInThisThread 保证
    //
    chaoxi::net::EventLoop loop;
    g_loop = &loop;

    // ========================================================================
    // 2. 创建 timerfd —— Linux 专用的定时器文件描述符
    // ========================================================================
    // timerfd_create 创建一个定时器 fd，当定时器超时时，该 fd 变为"可读"。
    // 这种设计的精妙之处：把"时间事件"统一为"I/O 事件"，
    // Reactor 只需要 poll 文件描述符，不需要单独的时间轮询。
    //
    // CLOCK_MONOTONIC: 单调时钟，不受系统时间调整影响
    // TFD_NONBLOCK:    fd 设为非阻塞模式
    // TFD_CLOEXEC:     exec 时自动关闭，防止子进程继承
    //
    int timefd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

    // ========================================================================
    // 3. 把 timerfd 包装成 Channel，设置回调并注册到 EventLoop
    // ========================================================================
    // Channel 是 fd 和 EventLoop 之间的桥梁：
    //   - 持有 fd 和感兴趣的事件类型（events_）
    //   - 当 poll 检测到事件时，Channel::handleEvent 根据 revents 分发回调
    //   - Channel 的生命周期由用户管理（非所有权），Poller 只持有裸指针
    //
    // 构造参数: Channel(EventLoop* loop, int fd)
    //   此时 Channel 尚未注册到 poller，addedToLoop_ == false
    //
    chaoxi::net::Channel channel(&loop, timefd);

    // 设置读事件回调 —— 当 timerfd 可读时，调用 timeout()
    channel.setReadCallback(timeout);

    // enableReading() 做了两件事：
    //   a) events_ |= kReadEvent  （设置感兴趣的事件为 POLLIN | POLLPRI）
    //   b) update() → addedToLoop_ = true → loop_->updateChannel(this)
    //      → Poller 将该 fd 加入 pollfds_ 数组，开始在 poll 中监控
    //
    channel.enableReading();

    // ========================================================================
    // 4. 设置 timerfd 的超时时间
    // ========================================================================
    // itimerspec 结构:
    //   it_value:  首次超时时间（相对时间，3 秒后触发）
    //   it_interval: 周期性超时时间（0 表示只触发一次）
    //
    // timerfd_settime(2) 是 Linux 系统调用，设置定时器参数，
    // 第三个参数 flags=0 表示使用相对时间
    //
    struct itimerspec howlong;
    bzero(&howlong, sizeof howlong);
    howlong.it_value.tv_sec = 3;          // 3 秒后首次（也是唯一一次）超时
    ::timerfd_settime(timefd, 0, &howlong, NULL);

    // ========================================================================
    // 5. 进入事件循环
    // ========================================================================
    // EventLoop::loop() 的核心逻辑：
    //
    //   while (!quit_) {
    //     activeChannels_.clear();
    //     pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    //     //                      ↑ 阻塞等待 I/O 事件，超时 10 秒
    //     //                      ↓ 遍历所有就绪的 Channel，逐个分发
    //     for (Channel* channel : activeChannels_) {
    //       currentActiveChannel_ = channel;
    //       channel->handleEvent(pollReturnTime_);
    //     }
    //     doPendingFunctors();  // 执行跨线程排队任务
    //   }
    //
    // 在本示例中:
    //   - poll 阻塞在 ::poll 上，等待 timerfd 可读（或 10 秒超时）
    //   - 3 秒后 timerfd 变为可读，poll 返回，activeChannels_ 包含我们的 channel
    //   - channel->handleEvent() → 检测到 POLLIN → 调用 readCallback_ → timeout()
    //   - timeout() 调用 loop.quit()，设置 quit_ = true
    //   - 当前迭代结束后，while 条件不满足，loop() 返回
    //
    std::println("Start to loop!");
    loop.loop();

    // ========================================================================
    // 6. 事件循环退出后的清理工作（顺序至关重要！）
    // ========================================================================

    // step 1: disableAll() — 清除所有感兴趣的事件
    //   内部调用 update()，通知 Poller 该 fd 不再需要监控。
    //   Poller::updateChannel 检测到 isNoneEvent() 为 true，
    //   将 pollfd 的 fd 设为负值（标记为"忽略但保留槽位"）。
    //
    channel.disableAll();

    // step 2: remove() — 从 Poller 中彻底移除
    //   前置条件: isNoneEvent() == true（即必须先 disableAll）
    //   内部: addedToLoop_ = false → loop_->removeChannel(this)
    //   → Poller 从 channels_ map 中删除该条目，从 pollfds_ 数组中移除
    //   只有经过这一步，Channel 析构时 assert(!addedToLoop_) 才能通过
    //
    channel.remove();

    // step 3: close fd
    //   fd 的生命周期独立于 Channel，Channel 不拥有 fd，需要手动关闭
    //
    ::close(timefd);

    // ========================================================================
    // 7. main 返回 → 栈对象析构（构造的逆序）
    // ========================================================================
    //   析构顺序: channel 先 → loop 后
    //
    //   ~Channel(): 断言 !addedToLoop_（已通过 remove() 满足 ✓）
    //              断言 !eventHandling_（不在事件处理中 ✓）
    //              断言 !loop_->hasChannel(this)（已从 poller 移除 ✓）
    //
    //   ~EventLoop():
    //              wakeupChannel_->disableAll() + remove() → close(wakeupFd_)
    //              析构 poller_ 和 timerQueue_
    //
}  // main() 结束，所有资源被 RAII 自动清理
