#pragma once

///
/// @file EventLoopThreadPool.hpp
/// @brief IO 线程池 —— 管理一组 EventLoop 线程，实现多线程 Reactor
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║           EventLoopThreadPool —— 多 Reactor 架构的核心               ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║  为什么需要线程池？                                                   ║
/// ║  ─────────────────                                                   ║
/// ║  单线程 Reactor 模型中，所有 I/O 都在一个线程：                       ║
/// ║    - CPU 利用率低（多核闲置）                                         ║
/// ║    - 一个耗时的回调会阻塞所有其他连接                                  ║
/// ║                                                                      ║
/// ║  多 Reactor 模型（one loop per thread + thread pool）：               ║
/// ║    - 一个 main loop（Acceptor 线程）：只负责 accept 新连接            ║
/// ║    - N 个 io loop（Worker 线程）：每线程一个 EventLoop，负责 I/O      ║
/// ║    - 新连接到来时，用 round-robin 或 hash 分配到某个 io loop          ║
/// ║                                                                      ║
/// ║  架构图（以 N=3 为例）：                                              ║
/// ║                                                                      ║
/// ║                        ┌─ Acceptor (main loop) ─┐                    ║
/// ║                        │ 只监听 listening socket  │                   ║
/// ║                        │ accept 新连接             │                   ║
/// ║                        └──────────┬───────────────┘                    ║
/// ║                                   │ 轮询分配                           ║
/// ║              ┌────────────────────┼────────────────────┐              ║
/// ║              ▼                    ▼                    ▼              ║
/// ║   ┌─── io loop 0 ───┐  ┌─── io loop 1 ───┐  ┌─── io loop 2 ───┐      ║
/// ║   │ EventLoop +     │  │ EventLoop +     │  │ EventLoop +     │      ║
/// ║   │ conn_a, conn_d  │  │ conn_b, conn_e  │  │ conn_c, conn_f  │      ║
/// ║   │ (读/写/回调)    │  │ (读/写/回调)    │  │ (读/写/回调)    │      ║
/// ║   └─────────────────┘  └─────────────────┘  └─────────────────┘      ║
/// ║                                                                      ║
/// ║  两种分发策略：                                                       ║
/// ║    - getNextLoop():      round-robin，保证负载均衡                   ║
/// ║    - getLoopForHash():   基于 hash，相同 hash 始终选择同一线程         ║
/// ║                          （适合需要"粘性"的业务场景）                  ║
/// ║                                                                      ║
/// ║  numThreads 的语义：                                                  ║
/// ║    0 (默认) → 不创建新线程，所有 I/O 在 baseLoop_ 中                  ║
/// ║    1       → 创建 1 个 io 线程，I/O 与 accept 分离                    ║
/// ║    N       → 创建 N 个 io 线程                                        ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝

#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace chaoxi::net
{
class EventLoop;
class EventLoopThread;

///
/// @brief IO 线程池：管理和调度一组 EventLoop 线程
///
/// 使用流程 (由 TcpServer 调用)：
///   1. 构造 → setThreadNum(N) → start(initCb)
///   2. 有新连接时调用 getNextLoop() 获取一个 io loop
///   3. 把新 TcpConnection 绑定到该 io loop 上
///
class EventLoopThreadPool
{
public:
    /// 线程初始化回调：当每个 io 线程的 EventLoop 创建后调用
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    /// @param baseLoop 主 Reactor 的 EventLoop（acceptor 所在线程）
    /// @param nameArg  线程池名称（每个线程命名为 nameArg0, nameArg1, ...）
    EventLoopThreadPool(EventLoop* baseLoop, std::string nameArg);
    ~EventLoopThreadPool();
    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool(EventLoopThreadPool&&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(EventLoopThreadPool&&) = delete;

    /// @brief 设置 io 线程数量（必须在 start() 前调用）
    /// @param numThreads 0=单线程, 1=独立 io 线程, N=N个 io 线程
    void setThreadNum(unsigned numThreads) { numThreads_ = numThreads; }

    /// @brief 启动线程池：创建所有 io 线程并让它们开始各自的事件循环
    /// @param cb 每个 io 线程启动后的初始化回调（可选，用于设置线程局部数据）
    void start(const ThreadInitCallback& cb = {});

    /// @brief round-robin 轮询获取下一个 io loop
    /// @return 如果 numThreads==0 返回 baseLoop_，否则返回当前轮到的 io loop
    [[nodiscard]] EventLoop* getNextLoop() noexcept;

    /// @brief 基于 hash 获取 io loop（相同 hash 值始终返回同一个 loop）
    /// @param hashCode 通常是客户端 IP/port 的 hash 值
    [[nodiscard]] EventLoop* getLoopForHash(size_t hashCode) noexcept;

    /// @brief 获取所有 io loop（包括 baseLoop_ 如果 numThreads==0）
    [[nodiscard]] std::span<EventLoop*> getAllLoops();

    [[nodiscard]] bool started() const noexcept { return started_; }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    EventLoop* baseLoop_;     ///< acceptor 所在主 EventLoop
    std::string name_;        ///< 线程池名称
    bool started_{false};     ///< 是否已启动
    unsigned numThreads_{0};  ///< io 线程数量
    unsigned next_{0};        ///< round-robin 游标

    /// io 线程对象数组（每个封装了一个 EventLoopThread + EventLoop）
    std::vector<std::unique_ptr<EventLoopThread>> threads_;

    /// 每个 io 线程的 EventLoop 裸指针（方便快速索引，不拥有所有权）
    std::vector<EventLoop*> loops_;
};
}  // namespace chaoxi::net
