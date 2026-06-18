#pragma once

///
/// @file EPollPoller.hpp
/// @brief epoll(7) I/O 多路复用 —— Linux 上最高效的 Poller 实现
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║         EPollPoller vs PollPoller —— 为什么 epoll 更快               ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║  poll(2) 的问题：                                                    ║
/// ║  ────────────────                                                    ║
/// ║  每次 poll() 调用时，内核需要遍历整个 pollfds_ 数组来检查事件。      ║
/// ║  O(N) 复杂度（N = 监控的 fd 数量）。                                  ║
/// ║  如果有 10,000 个连接，每次 poll 都要扫描 10,000 个 fd。              ║
/// ║                                                                      ║
/// ║  epoll(7) 的优势：                                                   ║
/// ║  ─────────────────                                                   ║
/// ║  epoll 使用**事件驱动**而非轮询：                                    ║
/// ║    1. epoll_ctl(ADD/MOD/DEL) 向内核注册"感兴趣的事件"                ║
/// ║       └─ 内核维护一棵红黑树存储所有被监控的 fd                        ║
/// ║    2. epoll_wait() —— 内核只返回**真正有事件发生的 fd**              ║
/// ║       └─ 内核用就绪链表存储活跃 fd，O(1) 获取                         ║
/// ║    3. 复杂度：O(活跃 fd 数量) 而非 O(总 fd 数量)                     ║
/// ║                                                                      ║
/// ║  对比表：                                                             ║
/// ║  ┌──────────────┬───────────────┬───────────────┐                    ║
/// ║  │              │   poll(2)     │   epoll(7)    │                    ║
/// ║  ├──────────────┼───────────────┼───────────────┤                    ║
/// ║  │ 注册机制     │ 每次重新传入   │ epoll_ctl 一次 │                   ║
/// ║  │              │ pollfds_ 数组  │ 注册，长期有效 │                   ║
/// ║  │ 事件获取     │ 遍历全部 fd    │ 只返回活跃 fd  │                   ║
/// ║  │ 时间复杂度   │ O(N)          │ O(active)      │                    ║
/// ║  │ 边缘触发支持 │ 不支持        │ EPOLLET 支持   │                    ║
/// ║  │ fd 数量增长  │ 线性变慢      │ 几乎零影响     │                   ║
/// ║  └──────────────┴───────────────┴───────────────┘                    ║
/// ║                                                                      ║
/// ║  Channel::index 的三个状态（epoll 特有）：                            ║
/// ║  ────────────────────────────────────────                             ║
/// ║  epoll 不需要像 PollPoller 那样维护 pollfds_ 数组，但它需要知道：     ║
/// ║  这个 Channel 当前在内核中的注册状态是怎样的。                        ║
/// ║                                                                      ║
/// ║    kNew (-1)    → 刚创建，未注册，channels_ map 中不存在             ║
/// ║    kAdded (1)   → 已通过 epoll_ctl(ADD) 注册到内核                   ║
/// ║                    channels_ map 中存在                               ║
/// ║    kDeleted (2) → 已通过 epoll_ctl(DEL) 从内核删除                   ║
/// ║                    channels_ map 中仍存在（直到 removeChannel）       ║
/// ║                                                                      ║
/// ║  状态转换：                                                           ║
/// ║                                                                      ║
/// ║     构造时 ──▶ kNew                                                  ║
/// ║        │  enableReading/updateChannel                                 ║
/// ║        ▼                                                             ║
/// ║     kAdded ◀── updateChannel (events != 0)                           ║
/// ║        │  disableAll → isNoneEvent                                   ║
/// ║        ▼                                                             ║
/// ║     kDeleted ──▶ removeChannel ──▶ kNew (channels_.erase)           ║
/// ║        │                                                             ║
/// ║        └── updateChannel (events != 0, re-enable) → kAdded          ║
/// ║                                                                      ║
/// ║  为什么需要 kDeleted 状态？                                           ║
/// ║  ─────────────────────────                                           ║
/// ║  当 disableAll() 时，只从 epoll 内核表中删除，但 channels_ map 保留。  ║
/// ║  这样 enableReading() 可以快速重新 ADD，因为 Channel 对象还在 map 中。 ║
/// ║  kDeleted 就是记录"Channel 还在 map 但不在 epoll 中"。                ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝

#include "chaoxi/net/Poller.hpp"

#include <vector>

struct epoll_event;

namespace chaoxi::net {

///
/// @brief 基于 epoll(7) 的 I/O 多路复用实现
///
/// EPollPoller 是 Poller 的子类，用 Linux 的 epoll 系统调用替代 poll。
/// 在 Linux 平台下这是推荐的 Poller 实现，比 PollPoller 高效。
///
/// 核心概念：
///   - epollfd_:  epoll_create1() 返回的 epoll 实例 fd
///   - events_:   用于接收 epoll_wait 返回的就绪事件数组（动态扩容）
///   - Channel::index 用于追踪 Channel 在 epoll 中的状态
///
/// 使用方式（通常由 EventLoop 内部自动选择）：
///   Poller::newDefaultPoller(loop) 返回 EPollPoller 或 PollPoller。
///
class EPollPoller : public Poller {
public:
    explicit EPollPoller(EventLoop* loop);
    ~EPollPoller() override;
    EPollPoller(const EPollPoller&) = delete;
    EPollPoller(EPollPoller&&) = delete;
    EPollPoller& operator=(const EPollPoller&) = delete;
    EPollPoller& operator=(EPollPoller&&) = delete;

    /// @brief 等待 I/O 事件（阻塞）
    /// 内部调用 epoll_wait()，返回后将活跃 Channel 填入 activeChannels
    [[nodiscard]] Timestamp poll(int timeoutMs,
                                 ChannelList* activeChannels) override;

    /// @brief 更新 Channel 的监听事件
    /// 根据 index 选择 epoll_ctl ADD / MOD / DEL 操作
    void updateChannel(Channel* channel) noexcept override;

    /// @brief 从 epoll 中彻底移除 Channel
    void removeChannel(Channel* channel) noexcept override;

private:
    /// epoll_wait 返回的 events_ 数组初始大小
    /// 如果一次返回的活跃事件数达到此大小，数组会自动扩容为 2 倍
    static constexpr int kInitEventListSize = 16;

    /// @brief 遍历 epoll_wait 返回的 events_ 数组，填充 activeChannels
    ///
    /// epoll_wait 把每个就绪 fd 的信息存入 events_[i]：
    ///   - events_[i].events:   实际发生的事件（POLLIN/POLLOUT/...）
    ///   - events_[i].data.ptr: 我们注册时设置的 Channel* 指针
    ///
    /// 通过 data.ptr 找回对应的 Channel，设置其 revents_，然后加入 activeChannels。
    ///
    void fillActiveChannels(int numEvents,
                            ChannelList* activeChannels) const;

    /// @brief 执行 epoll_ctl 系统调用
    /// @param operation EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
    /// @param channel   要操作的 Channel
    void update(int operation, Channel* channel) noexcept;

    using EventList = std::vector<struct epoll_event>;

    int epollfd_;       ///< epoll_create1() 返回的 epoll 实例 fd
    EventList events_;  ///< epoll_wait 的就绪事件数组（动态扩容）
};

}  // namespace chaoxi::net
