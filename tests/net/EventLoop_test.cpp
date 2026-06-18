///
/// @file tests/net/EventLoop_test.cpp
/// @brief EventLoop 的"一线一循环"线程模型测试
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║           muduo Reactor 模式 —— 核心概念速览                          ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║  Reactor（反应堆）是什么？                                            ║
/// ║  ─────────────────────────                                           ║
/// ║  想象你是一个餐厅的前台接待员。你不能同时干两件事，但你需要处理：      ║
/// ║   - 客人进门（新连接）                                                ║
/// ║   - 客人举手要点菜（数据到达）                                        ║
/// ║   - 后厨上菜好了通知你（定时器到期）                                  ║
/// ║                                                                      ║
/// ║  Reactor 就是你——一个线程，一个死循环，不断问："谁有事？"             ║
/// ║  然后依次处理。muduo 把一切 I/O 事件、时间事件都统一为"文件描述符是否  ║
/// ║  可读/可写"，用一个 poll/epoll 系统调用统一监听。                     ║
/// ║                                                                      ║
/// ║  三大核心组件：                                                       ║
/// ║  ┌─────────────┐   ┌──────────────┐   ┌──────────────────┐           ║
/// ║  │  EventLoop   │──▶│    Poller    │──▶│  poll/epoll      │           ║
/// ║  │  (事件循环)  │   │ (I/O 多路复用)│   │  (系统调用)      │           ║
/// ║  │              │◀──│              │◀──│                  │           ║
/// ║  │  "谁有事？"  │   │ "监控所有 fd" │   │ "fd1 可读了！"    │          ║
/// ║  │  "你来处理"  │   └──────────────┘   └──────────────────┘           ║
/// ║  │      │                                                             ║
/// ║  │      │  ◀── fd 被包装成 Channel，Channel 上挂着用户回调              ║
/// ║  │      │                                                             ║
/// ║  │      ▼                                                             ║
/// ║  ┌─────────────┐   ┌──────────────┐                                  ║
/// ║  │   Channel    │   │  TimerQueue  │                                  ║
/// ║  │ fd + 回调的  │   │  定时器队列   │                                  ║
/// ║  │     封装     │   │ (内部也用 fd) │                                  ║
/// ║  └─────────────┘   └──────────────┘                                  ║
/// ║                                                                      ║
/// ║  一线一循环（one loop per thread）原则：                              ║
/// ║  ─────────────────────────────────────                               ║
/// ║  每个线程至多拥有一个 EventLoop。这是 muduo 最核心的线程安全模型。    ║
/// ║  因为 EventLoop 的所有操作（改 fd 监听列表、调用回调）都必须在自己的  ║
/// ║  线程中执行，不需要锁。跨线程通信通过 runInLoop/queueInLoop +         ║
/// ║  wakeupFd 来唤醒对端线程。                                            ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝
///
/// 本测试验证的内容：
///   1. 主线程能创建并运行 EventLoop
///   2. 子线程能独立创建并运行另一个 EventLoop（互不干扰）
///   3. EventLoop 上的定时器能正确触发
///   4. 一个线程尝试创建第二个 EventLoop 会触发 FATAL 错误
///   5. getEventLoopOfCurrentThread() 的行为正确
///

#include "chaoxi/base/CurrentThread.hpp"  // thread_local tid 缓存
#include "chaoxi/net/EventLoop.hpp"       // Reactor 事件循环

#include <cassert>
#include <print>
#include <thread>

#include <unistd.h>

using namespace chaoxi::net;

// ============================================================================
// callback() —— 定时器回调函数
// ============================================================================
///
/// 这个函数由子线程的 EventLoop 在 timer 到期时调用。
/// 它做了两件事：
///   1. 打印当前线程信息
///   2. 故意尝试创建第二个 EventLoop —— 这会触发 "一线一循环" 的 FATAL 检查
///
/// 为什么要故意犯规？
///   这是测试代码，用来验证 EventLoop 的自我保护机制确实生效：
///   当你在已有 EventLoop 的线程里再创建一个 EventLoop 时，
///   LOG_FATAL 会触发 std::abort()，程序终止。
///
void callback() {
    // 打印回调所在线程的进程ID和线程ID
    // getpid():   Linux 系统调用，获取进程ID（同一进程的所有线程共享）
    // CurrentThread::tid(): muduo 缓存的线程ID（类似 gettid()）
    std::println("callback(): pid={}, tid={}", getpid(),
                 chaoxi::CurrentThread::tid());

    // ⚠️ 这里故意在同一线程中创建第二个 EventLoop
    // 此时子线程已经有一个 EventLoop（threadFunc 中的 loop），
    // t_loopInThisThread（thread_local 变量）不为 null，
    // EventLoop 构造函数会触发 LOG_FATAL → std::abort() → 进程终止
    EventLoop anotherLoop;
}

// ============================================================================
// threadFunc() —— 子线程入口函数
// ============================================================================
///
/// 子线程的职责是：
///   1. 创建自己的 EventLoop
///   2. 注册一个 1 秒后触发的定时器
///   3. 进入事件循环
///   4. 1 秒后 callback() 被调用，进程因 FATAL 终止
///
/// 注意：这个线程会因为在 callback() 中创建一个重复的 EventLoop 而触发 abort，
///       进程会非正常退出。这不是 bug，是测试的一部分——验证保护机制正常工作。
///
void threadFunc() {
    // ────────────────────────────────────────────────────────────────────
    // Step 1: 打印子线程身份
    // ────────────────────────────────────────────────────────────────────
    // 你会看到两行 "threadFunc(): ..." 输出：
    //   第一行来自 main()      → pid=12345, tid=12345  （主线程）
    //   第二行来自 threadFunc() → pid=12345, tid=12346  （子线程）
    // pid 相同说明它们是同一进程，tid 不同说明它们是不同线程
    //
    std::println("threadFunc(): pid={}, tid={}", getpid(),
                 chaoxi::CurrentThread::tid());

    // ────────────────────────────────────────────────────────────────────
    // Step 2: 验证当前线程还没有 EventLoop
    // ────────────────────────────────────────────────────────────────────
    // getEventLoopOfCurrentThread() 返回的是 thread_local 指针
    // 线程刚启动时，没有任何 EventLoop，应该返回 nullptr
    //
    assert(EventLoop::getEventLoopOfCurrentThread() == nullptr);

    // ────────────────────────────────────────────────────────────────────
    // Step 3: 创建子线程的 EventLoop
    // ────────────────────────────────────────────────────────────────────
    //
    // EventLoop 构造函数内部做了什么？（看源码 chaoxi/net/EventLoop.cpp:54）
    //
    //   EventLoop::EventLoop()
    //     : threadId_(CurrentThread::tid())     ← ① 记录所属线程 ID
    //     , poller_(Poller::newDefaultPoller(this))  ← ② 创建 Poller（底层是 poll/epoll）
    //     , timerQueue_(make_unique<TimerQueue>()) ← ③ 创建定时器队列
    //     , wakeupFd_(createEventfd())          ← ④ 创建跨线程唤醒用的 eventfd
    //     , wakeupChannel_(make_unique<Channel>(this, wakeupFd_)) ← ⑤ 把 eventfd 包装成 Channel
    //   {
    //     // ⑥ 检查"一线一循环"约束
    //     if (t_loopInThisThread) {             ← 已有 EventLoop？→ FATAL
    //         LOG_FATAL << "Another EventLoop exists...";
    //     }
    //     t_loopInThisThread = this;            ← ⑦ 登记自己
    //     wakeupChannel_->enableReading();      ← ⑧ 让 poll 监听 eventfd
    //   }
    //
    //   图解 EventLoop 内部结构：
    //
    //   ┌─────────────────── EventLoop ───────────────────┐
    //   │  threadId_ = 12346   (这条线程的 tid)            │
    //   │                                                   │
    //   │  ┌─ Poller ──────────────┐  ┌─ TimerQueue ────┐ │
    //   │  │ channels_ map:        │  │ timerfd_        │ │
    //   │  │   wakeupFd_ → Channel │  │ timerfdChannel_ │ │
    //   │  │   timerfd_  → Channel │  │ timers_ set     │ │
    //   │  │ pollfds_ array        │  └─────────────────┘ │
    //   │  └───────────────────────┘                       │
    //   │                                                   │
    //   │  wakeupFd_ (eventfd)  ←── 跨线程唤醒通道          │
    //   │  wakeupChannel_       ←── 包装 wakeupFd_          │
    //   └───────────────────────────────────────────────────┘
    //
    EventLoop loop;

    // 创建后，getEventLoopOfCurrentThread() 应该返回刚才创建的 loop 的地址
    assert(EventLoop::getEventLoopOfCurrentThread() == &loop);

    // ────────────────────────────────────────────────────────────────────
    // Step 4: 注册一个 1 秒后触发的定时器
    // ────────────────────────────────────────────────────────────────────
    //
    // loop.runAfter(1.0, callback) 的调用链：
    //
    //   runAfter(delay=1.0, cb=callback)
    //   │
    //   │ 计算到期时间: when = now + 1.0秒
    //   │
    //   ▼
    //   EventLoop::runAt(when, cb)
    //   │
    //   ▼
    //   TimerQueue::addTimer(cb, when, interval=0.0)
    //   │
    //   │ ① 创建一个 Timer 对象（cb, when, interval）
    //   │ ② 调用 loop->runInLoop(lambda) 确保在 loop 线程执行
    //   │    因为当前就在 loop 线程，runInLoop 直接执行 lambda
    //   │
    //   ▼
    //   addTimerInLoop(timer)
    //   │
    //   │ ③ 把 Timer 插入 timers_ 的 std::set（按到期时间排序）
    //   │ ④ 如果新 timer 是最早到期的，通过 timerfd_settime() 重置 timerfd
    //   │
    //   ▼
    //   timerfd_settime(timerfd_, 1.0秒)
    //   │
    //   │ ⑤ 告诉内核："timerfd 在 1 秒后变为可读"
    //   │    注意：timerfd 也是文件描述符！poll 会监听它，就像监听 socket 一样
    //   │
    //   └──→ 返回 TimerId（可用于取消）
    //
    //   为什么用 timerfd 而不是单独的时间轮？
    //   ─────────────────────────────────────
    //   因为 Reactor 模式的核心思想是"一切事件都是 I/O"。
    //   把定时器也变成 fd 的好处：EventLoop 只需要一个 poll 调用，
    //   同时等待 I/O 事件和时间事件，代码极其简洁统一。
    //
    //   在 poll 的视角里，wakeupFd_ 和 timerfd_ 都是普通的 fd：
    //   ┌──────────────┐
    //   │ poll 等待     │
    //   │ fd 列表:      │
    //   │   wakeupFd_   │← 有人跨线程通知时变为可读
    //   │   timerfd_    │← 定时器到期时变为可读
    //   │   ...其他 fd  │
    //   └──────────────┘
    //
    loop.runAfter(1.0, callback);
    //
    // 此时堆上的 Timer 对象状态：
    //   callback_  → callback 函数指针
    //   expiration_ → now + 1.0秒
    //   interval_  → 0.0 (不重复)
    //   repeat_    → false

    // ────────────────────────────────────────────────────────────────────
    // Step 5: 进入事件循环（阻塞！）
    // ────────────────────────────────────────────────────────────────────
    //
    // loop.loop() 的内部逻辑（简化为伪代码）：
    //
    //   void EventLoop::loop() {
    //       assert(!looping_);          // 不能重复调用
    //       assertInLoopThread();       // 必须在自己的线程
    //       looping_ = true;
    //       quit_ = false;
    //
    //       while (!quit_) {            // ① 循环直到有人调用 quit()
    //           activeChannels_.clear();
    //
    //           // ② 阻塞等待 I/O 事件（或超时 10 秒）
    //           pollReturnTime_ = poller_->poll(10*1000, &activeChannels_);
    //
    //           // ③ 遍历所有就绪的 Channel，分发事件
    //           for (Channel* ch : activeChannels_) {
    //               ch->handleEvent(pollReturnTime_);
    //           }
    //
    //           // ④ 执行排队的跨线程任务
    //           doPendingFunctors();
    //       }
    //       looping_ = false;
    //   }
    //
    //  当 poll 阻塞在 ::poll() 上时，线程几乎不消耗 CPU。
    //  一但有任何 fd 就绪（包括 timerfd），poll 返回，事件被分发。
    //
    //  现在这个 EventLoop 监控着哪些 fd？
    //    1. wakeupFd_ (eventfd)   — 用于跨线程唤醒
    //    2. timerfd_ (timerfd)    — 1 秒后到期
    //
    loop.loop();
    //
    //  注意：正常情况下 loop.loop() 会在 quit() 被调用后返回。
    //  但在这个测试中，callback() 会触发 std::abort() 导致进程终止，
    //  所以实际上永远不会执行到 loop.loop() 之后。

    // 以下代码永远不会被执行到（进程已在 callback 中 abort）
    // 如果执行到这里，说明 callback() 没有触发 FATAL —— 那也是个 bug
}

// ============================================================================
// main() —— 主线程入口
// ============================================================================
///
/// 主线程负责：
///   1. 打印主线程信息
///   2. 创建主线程的 EventLoop
///   3. 启动子线程
///   4. 进入自己的事件循环
///   5. 等子线程结束后 join
///
/// 整个程序的时序图：
///
///   时间轴 ──────────────────────────────────────────────────────▶
///
///   主线程 (tid=12345):
///   │ 创建 loop_main
///   │ 打印 "threadFunc(): pid=..., tid=12345"
///   │ spawn 子线程 ──┐
///   │ loop_main.loop() → poll 阻塞等待事件...
///   │ (永远没人调用 quit，主线程卡在这里)  ......................
///   │
///   子线程 (tid=12346):                  │
///                              打印 "threadFunc(): pid=..., tid=12346"
///                              创建 loop_child
///                              loop_child.runAfter(1.0, callback)
///                              loop_child.loop() → poll 阻塞
///                                                    │
///                                          ~1秒后 timerfd 可读
///                                          poll 返回
///                                          callback() 被调用
///                                          打印 "callback(): ..."
///                                          EventLoop anotherLoop; ← 冲突！
///                                          LOG_FATAL → std::abort()
///                                          进程终止 (exit code 134)
///
///   关键观察：
///   - 主线程和子线程各有一个独立的 EventLoop，互不干扰
///   - 主线程的 loop.loop() 永远阻塞（没有人 quit），但没关系——
///     子线程的 abort() 会终止整个进程
///   - 如果主线程也需要优雅退出，应该在 callback() 中通过 runInLoop
///     通知主线程的 loop 调用 quit()
///
int main() {
    // ────────────────────────────────────────────────────────────────────
    // Step 1: 打印主线程身份
    // ────────────────────────────────────────────────────────────────────
    std::println("threadFunc(): pid={}, tid={}", getpid(),
                 chaoxi::CurrentThread::tid());

    // ────────────────────────────────────────────────────────────────────
    // Step 2: 验证当前线程还没有 EventLoop
    // ────────────────────────────────────────────────────────────────────
    assert(EventLoop::getEventLoopOfCurrentThread() == nullptr);

    // ────────────────────────────────────────────────────────────────────
    // Step 3: 创建主线程的 EventLoop
    // ────────────────────────────────────────────────────────────────────
    // 和 threadFunc 中一样的构造过程。
    // 区别：主线程的 EventLoop 没有任何定时器，只有 wakeupFd_ 被监听。
    // 所以它的 poll 永远只会超时返回（每 10 秒一次），不会处理任何实际事件。
    //
    EventLoop loop;
    assert(EventLoop::getEventLoopOfCurrentThread() == &loop);

    // ────────────────────────────────────────────────────────────────────
    // Step 4: 创建子线程
    // ────────────────────────────────────────────────────────────────────
    // std::thread 构造时就开始执行 threadFunc。
    // 从这一刻起，程序有两个线程在并行运行：
    //   - 主线程：即将进入 loop.loop()
    //   - 子线程：正在 threadFunc() 中创建自己的 EventLoop
    //
    std::thread thread(threadFunc);

    // ────────────────────────────────────────────────────────────────────
    // Step 5: 主线程进入事件循环（阻塞！）
    // ────────────────────────────────────────────────────────────────────
    // 主线程的 loop 只监听了 wakeupFd_，没有其他 fd 和定时器。
    // 所以它会在 poll 中每 10 秒超时一次，醒来检查 quit_，继续 poll……
    // 没有人会调用主线程 loop 的 quit()，所以理论上永远循环。
    //
    // 实际上：1 秒后子线程的 callback() 触发 abort()，进程终止。
    //         所以主线程的 loop 最多运行约 1 秒就被强制结束了。
    //
    loop.loop();

    // ────────────────────────────────────────────────────────────────────
    // Step 6: join 子线程（永远不会执行到）
    // ────────────────────────────────────────────────────────────────────
    // thread.join() 需要等到子线程结束，但子线程不会正常结束——
    // callback() 中的 abort() 直接终止进程。
    // 所以这行代码永远不会被执行。
    //
    thread.join();

    // ========================================================================
    // 补充知识：如何让这个程序优雅退出？
    // ========================================================================
    //
    // 如果想让程序正常退出而不是 abort，可以这样修改 callback():
    //
    //   void callback() {
    //       std::println("callback(): tid={}", CurrentThread::tid());
    //       // 不要创建 EventLoop anotherLoop; ← 删掉这行
    //
    //       // 跨线程通知主线程的 EventLoop 退出
    //       EventLoop* mainLoop = ...; // 需要某种方式拿到主线程 loop 的指针
    //       mainLoop->quit();          // 设置主线程 loop 的 quit_ 标志
    //                                    // 但 quit() 不在主线程调用，
    //                                    // 所以内部会 wakeup() 唤醒主线程
    //   }
    //
    // quit() 的跨线程工作原理：
    //   1. 设置 quit_ = true
    //   2. 检测到不在主线程 → 调用 wakeup()
    //   3. wakeup() 向 wakeupFd_ 写入 8 字节
    //   4. 主线程的 poll 检测到 wakeupFd_ 可读，立即返回
    //   5. while (!quit_) 检查到 quit_ 为 true，退出循环
    //
    // 这正是 muduo 跨线程通信的核心模式：修改标志位 + wakeup 唤醒。
}

///
/// @section 运行结果解读
///
/// 程序可能的完整输出：
///
///   threadFunc(): pid=27295, tid=27295       ← main()    打印
///   threadFunc(): pid=27295, tid=27296       ← threadFunc() 打印
///   callback():   pid=27295, tid=27296       ← callback() 打印（1秒后）
///   20260607 ... FATAL Another EventLoop ... ← LOG_FATAL  输出
///   Aborted (core dumped)                    ← std::abort()
///   exit code: 134 (128 + SIGABRT 6)
///
/// 输出解读：
///   - pid 相同 (27295) → 主线程和子线程属于同一个进程 ✓
///   - tid 不同          → 它们是不同的线程 ✓
///   - callback 在 tid=27296 执行 → 定时器在子线程的 EventLoop 中触发 ✓
///   - FATAL 输出         → "一线一循环"约束被违反，检查生效 ✓
///   - 134 退出码         → 128 + 6(SIGABRT)，确认是 abort() 终止 ✓
///
/// @section muduo 源码阅读路线
///
/// 理解了本测试后，按以下顺序阅读源码效果最好：
///
///   1  EventLoop.hpp/cpp    —— Reactor 核心，事件循环
///   2  Channel.hpp/cpp      —— fd 的事件分发器
///   3  Poller.hpp/cpp       —— I/O 多路复用封装 (基类)
///   4  PollPoller.hpp/cpp   —— poll(2) 的具体实现
///   5  TimerQueue.hpp/cpp   —— 基于 timerfd 的定时器队列
///   6  Timer.hpp/cpp        —— 单个定时器对象
///   7  TimerId.hpp          —— 定时器的"取消令牌"
///   8  EventLoopThread.hpp/cpp —— EventLoop + 线程的组合（进阶）
///   9  TcpConnection.hpp/cpp   —— TCP 连接管理（应用层）
///
