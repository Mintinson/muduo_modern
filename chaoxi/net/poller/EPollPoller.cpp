///
/// @file EPollPoller.cpp
/// @brief EPollPoller 实现 —— epoll(7) 的核心操作：CTL + WAIT
///

#include "chaoxi/net/poller/EPollPoller.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <sys/epoll.h>
#include <unistd.h>

namespace chaoxi::net
{
namespace
{

///
/// Channel::index 的三个状态常量：
///
///   kNew (-1):   刚创建或已被 removeChannel，未在内核注册。
///                此时 channels_ map 中不存在此 Channel。
///
///   kAdded (1):  已通过 epoll_ctl(EPOLL_CTL_ADD) 注册到内核 epoll 实例。
///                channels_ map 中存在。
///
///   kDeleted (2): 已通过 epoll_ctl(EPOLL_CTL_DEL) 从内核删除（调用了
///   disableAll），
///                 但 channels_ map 中仍然存在。下次 enableReading 时可以直接
///                 用 EPOLL_CTL_ADD 重新注册，无需重建 map 条目。
///
/// 为什么要区分 kNew 和 kDeleted？
///   - kNew  → 需要先在 channels_ map 中插入
///   - kDeleted → channels_ map 中已有，直接用 ADD 重新注册
///
constexpr int kNew = -1;
constexpr int kAdded = 1;
constexpr int kDeleted = 2;

/// @brief 将 epoll_ctl 的 operation 枚举值转为可读字符串
[[nodiscard]] std::string_view operationToString(int op) noexcept
{
    switch (op)
    {
        case EPOLL_CTL_ADD:
            return "ADD";
        case EPOLL_CTL_DEL:
            return "DEL";
        case EPOLL_CTL_MOD:
            return "MOD";
        default:
            assert(false && "ERROR op");
            return "Unknown Operation";
    }
}
}  // namespace

// ============================================================================
// 构造 / 析构
// ============================================================================

///
/// @brief 构造 EPollPoller —— 创建 epoll 实例
///
/// epoll_create1(EPOLL_CLOEXEC):
///   - 创建一个 epoll 实例，返回一个 fd（epollfd_）
///   - EPOLL_CLOEXEC: exec 时自动关闭此 fd（防止子进程继承）
///   - 与旧的 epoll_create(size) 不同，size 参数被忽略（内核动态管理）
///
EPollPoller::EPollPoller(EventLoop* loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventListSize)  // 预分配 16 个 epoll_event 空间
{
    if (epollfd_ < 0)
    {
        LOG_SYSFATAL << "EPollPoller::EPollPoller";
    }
}

///
/// @brief 析构 —— 关闭 epoll 实例 fd
///
EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

// ============================================================================
// poll —— 核心：等待 I/O 事件
// ============================================================================

///
/// @brief 等待 I/O 事件发生（阻塞，直到有事件或超时）
///
/// 调用 epoll_wait() 等待事件：
///   - 有事件    → 返回就绪 fd 数量，填入 events_ 数组
///   - 超时      → 返回 0，什么都没发生
///   - 被信号中断 → 返回 -1，errno = EINTR（非致命）
///   - 其他错误   → 返回 -1，记录日志
///
/// 动态扩容策略：
///   如果本次返回的事件数量恰好等于 events_.size()（数组满了），
///   说明可能还有更多事件没被返回。将 events_ 扩容为 2 倍，
///   下次 epoll_wait 可以有更大容量。
///
/// @param timeoutMs      超时毫秒数（-1 表示无限等待）
/// @param activeChannels [out] 存放就绪 Channel 的列表
/// @return poll 返回的时刻（Timestamp）
///
Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    FLOG_TRACE("fd total count {}", channels_.size());

    // epoll_wait: 阻塞等待事件
    //   epollfd_       → epoll 实例
    //   events_.data() → 接收事件的数组
    //   events_.size() → 数组容量
    //   timeoutMs      → 超时时间
    int numEvents = ::epoll_wait(epollfd_, events_.data(),
                                 static_cast<int>(events_.size()), timeoutMs);

    int saveError = errno;
    Timestamp now = Timestamp::clock::now();  // 记录返回时刻

    if (numEvents > 0)
    {
        FLOG_TRACE("{} events happened", numEvents);
        // 将 events_ 中的就绪事件转换为 activeChannels
        fillActiveChannels(numEvents, activeChannels);

        // 动态扩容：如果本次返回了 capacity 个事件，下次可能更多
        if (static_cast<std::size_t>(numEvents) == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_TRACE << "nothing happened";
    }
    else
    {
        // numEvents < 0: 出错
        // EINTR 是正常的（被信号中断），不需要记录
        if (saveError != EINTR)
        {
            errno = saveError;
            LOG_SYSERR << "EPollPoller::poll()";
        }
    }
    return now;
}

// ============================================================================
// fillActiveChannels —— 将 epoll 事件转换为 Channel 列表
// ============================================================================

///
/// @brief 遍历 epoll_wait 返回的事件，填充 activeChannels
///
/// epoll 的精妙设计：
///   注册 Channel 时（epoll_ctl ADD），我们把 Channel* 存入
///   epoll_event.data.ptr。 当事件发生时，epoll_wait 在 events_[i].data.ptr
///   中返回同一个指针。 这实现了**零查找开销**——直接从就绪事件跳转到 Channel
///   对象。
///
///   对比 poll：
///     poll 返回 pollfd 数组，你需要根据 fd 编号去查找对应的 Channel（O(log
///     N)）。 epoll 直接把 Channel* 还给你（O(1)）。
///
/// @param numEvents      就绪事件数量
/// @param activeChannels [out] 存放活跃 Channel 的列表
///
void EPollPoller::fillActiveChannels(int numEvents,
                                     ChannelList* activeChannels) const
{
    assert(static_cast<size_t>(numEvents) <= events_.size());

    for (std::size_t i = 0; i < static_cast<size_t>(numEvents); ++i)
    {
        // data.ptr 就是我们注册时存入的 Channel* 指针
        auto* channel = static_cast<Channel*>(events_[i].data.ptr);

#ifndef NDEBUG
        // Debug 模式：验证 Channel 确实在 channels_ map 中
        int fd = channel->fd();
        auto it = channels_.find(fd);
        assert(it != channels_.end());
        assert(it->second == channel);
#endif

        // 把 epoll 返回的事件类型写入 Channel 的 revents_
        // 例如: EPOLLIN → POLLIN, EPOLLOUT → POLLOUT, EPOLLERR → POLLERR, ...
        channel->set_revents(static_cast<int>(events_[i].events));

        activeChannels->push_back(channel);
    }
}

// ============================================================================
// updateChannel —— 注册/修改/删除 一个 fd 的监听事件
// ============================================================================

///
/// @brief 更新 Channel 在 epoll 中的事件注册
///
/// 根据 Channel::index 的当前值决定使用什么 epoll_ctl 操作：
///
///   index == kNew (-1):
///     └─ 全新的 Channel → 加入 channels_ map → epoll_ctl(ADD)
///     └─ index → kAdded
///
///   index == kDeleted (2):
///     └─ 之前被 disableAll 从内核删除，现在重新启用 → epoll_ctl(ADD)
///     └─ index → kAdded
///
///   index == kAdded (1):
///     └─ 已在内核注册，需要修改事件：
///        ├─ isNoneEvent() → epoll_ctl(DEL) → index → kDeleted
///        └─ !isNoneEvent() → epoll_ctl(MOD)
///
///   关键区别（与 PollPoller 对比）：
///     PollPoller::updateChannel 用"负值 fd"标记忽略的 pollfd。
///     EPollPoller 用 epoll_ctl(DEL) 直接从内核删除，更彻底、更高效。
///
void EPollPoller::updateChannel(Channel* channel) noexcept
{
    Poller::assertInLoopThread();
    const int index = channel->index();
    LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events()
              << " index = " << index;

    if (index == kNew || index == kDeleted)
    {
        // ── 情况 A: 首次注册 / 重新注册 ──
        int fd = channel->fd();

        if (index == kNew)
        {
            // kNew: 全新的 Channel，需要先加入 channels_ map
            assert(channels_.find(fd) == channels_.end());
            channels_[fd] = channel;
        }
        else
        {  // index == kDeleted
            // kDeleted: 之前被删除，但仍在 channels_ map 中
            assert(channels_.find(fd) != channels_.end());
            assert(channels_[fd] == channel);
        }

        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD,
               channel);  // → epoll_ctl(epollfd_, ADD, fd, &event)
    }
    else
    {
        // ── 情况 B: 已注册，更新或删除 ──
        int fd = channel->fd();
        (void)fd;
        assert(channels_.find(fd) != channels_.end());
        assert(channels_[fd] == channel);
        assert(index == kAdded);

        if (channel->isNoneEvent())
        {
            // 用户不关心任何事件了 → 从 epoll 内核表中删除
            update(EPOLL_CTL_DEL,
                   channel);  // → epoll_ctl(epollfd_, DEL, fd, NULL)
            channel->set_index(kDeleted);  // 标记为"map中存在，但内核中已删除"
        }
        else
        {
            // 修改关注的事件类型（例如从只读变为读写）
            update(EPOLL_CTL_MOD,
                   channel);  // → epoll_ctl(epollfd_, MOD, fd, &event)
        }
    }
}

// ============================================================================
// removeChannel —— 从 epoll 中彻底移除
// ============================================================================

///
/// @brief 从 epoll 中彻底删除一个 Channel
///
/// 前置条件：Channel 必须是 isNoneEvent()（没有关注的事件），
///           通常是已经调用过 disableAll()。
///
/// 操作：
///   1. 如果在内核中注册过（kAdded），先 epoll_ctl(DEL)
///   2. 从 channels_ map 中删除
///   3. index → kNew（回到初始状态）
///
void EPollPoller::removeChannel(Channel* channel) noexcept
{
    Poller::assertInLoopThread();
    int fd = channel->fd();
    FLOG_TRACE("fd={}", fd);

    assert(channels_.contains(fd));
    assert(channels_[fd] == channel);
    assert(channel->isNoneEvent());

    int index = channel->index();

    // 可能在 kAdded 或 kDeleted 状态
    // kAdded:  还在内核中，需要先 DEL
    // kDeleted: 已经在内核中删除了，不需要再 DEL
    assert(index == kAdded || index == kDeleted);

    size_t n = channels_.erase(fd);
    (void)n;
    assert(n == 1);

    if (index == kAdded)
    {
        // 还在内核注册表中 → 需要显式删除
        update(EPOLL_CTL_DEL, channel);
    }
    // 如果 index == kDeleted，已经在 updateChannel 中被 DEL 过了

    channel->set_index(kNew);  // 标记为初始状态
}

// ============================================================================
// update —— epoll_ctl 的封装
// ============================================================================

///
/// @brief 执行 epoll_ctl 系统调用
///
/// @param operation EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
/// @param channel   要操作的 Channel（读取其 fd 和 events）
///
/// epoll_event 结构：
///   .events   = 我们关注的事件（POLLIN | POLLOUT | ...）
///   .data.ptr = Channel* 指针（事件发生时通过此指针找回 Channel）
///
/// 错误处理：
///   - EPOLL_CTL_DEL 失败 → 只记录错误（不 fatal），因为 fd 可能已被内核自动移除
///   - EPOLL_CTL_ADD/MOD 失败 → fatal，因为这是不应该发生的编程错误
///
void EPollPoller::update(int operation, Channel* channel) noexcept
{
    struct epoll_event event{};
    event.events = static_cast<uint32_t>(channel->events());  // 我们关注的事件类型
    event.data.ptr = channel;          // 存入 Channel*，epoll_wait 时原样返回

    int fd = channel->fd();

    FLOG_TRACE("epoll_ctl op={} fd={} event={}", operationToString(operation),
               fd, channel->eventsToString());

    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            // DEL 失败可以容忍：内核可能在 fd close 时自动清理
            FLOG_SYSERR("epoll_ctl op={} fd={}", operationToString(operation),
                        fd);
        }
        else
        {
            // ADD/MOD 失败是严重错误，直接 abort
            FLOG_SYSFATAL("epoll_ctl op={} fd={}", operationToString(operation),
                          fd);
        }
    }
}

}  // namespace chaoxi::net
