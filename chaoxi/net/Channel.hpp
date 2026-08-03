#pragma once

///
/// @file Channel.hpp
/// @brief fd 事件分发器 —— 对文件描述符及其事件的封装，Reactor 模式的核心抽象
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║              Channel —— fd + 事件 + 回调 的三元组                      ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║  Channel 不拥有 fd，只记录 fd 和关注的事件类型。                         ║
/// ║  Poller 检测到事件后，通过 Channel 的回调分发给用户代码。                  ║
/// ║                                                                      ║
/// ║  数据流:                                                               ║
/// ║                                                                      ║
/// ║    应用程序            Channel            Poller                       ║
/// ║       │                  │                  │                          ║
/// ║       │  enableReading() │                  │                          ║
/// ║       ├─────────────────▶│  update()        │                          ║
/// ║       │                  ├─────────────────▶│  epoll_ctl ADD            ║
/// ║       │                  │                  │                          ║
/// ║       │       ... 内核检测到 fd 可读 ...       │                          ║
/// ║       │                  │                  │                          ║
/// ║       │                  │  handleEvent() ◀──│  poll 返回               ║
/// ║       │  readCallback  ◀─┤                  │                          ║
/// ║       │    (用户代码)     │                  │                          ║
/// ║                                                                      ║
/// ║  三种事件类型：                                                         ║
/// ║    kReadEvent  = POLLIN | POLLPRI   — 可读                           ║
/// ║    kWriteEvent = POLLOUT            — 可写                            ║
/// ║    kNoneEvent  = 0                  — 无事件                           ║
/// ║                                                                      ║
/// ║  生命周期要点：                                                         ║
/// ║    - Channel 必须在 EventLoop 线程中构造和析构                            ║
/// ║    - 析构时断言 addedToLoop_ == false（必须已 remove）                   ║
/// ║    - 析构时断言 eventHandling_ == false（不能正在回调中销毁）               ║
/// ║    - tie() 机制防止回调时 owner 对象被销毁                                 ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝
///

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Platform.hpp"

#include <functional>
#include <memory>
#include <string>

namespace chaoxi::net {

class EventLoop;

///
/// @brief fd 的 I/O 事件分发器
///
/// 每个 Channel 关联一个 fd，以及该 fd 感兴趣的 I/O 事件类型和对应的回调。
/// Channel 通过 EventLoop → Poller 注册到 epoll/poll。
///
/// 典型使用方式：
///   1. 创建 Channel(EventLoop*, int fd)
///   2. setReadCallback / setWriteCallback / setCloseCallback / setErrorCallback
///   3. enableReading() 开始监听读事件
///   4. 事件到达时 Channel 自动调用对应的回调
///   5. disableAll() + remove() 在析构前清理
///
class Channel {
public:
    // using ReadEventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp receiveTime)>;
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, SocketHandle fd) : loop_(loop), fd_(fd) {}

    ~Channel();
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    Channel(Channel&&) noexcept = delete;
    Channel& operator=(Channel&&) noexcept = delete;

    /// @brief 事件总入口 —— 由 EventLoop 在 poll 返回后调用
    void handleEvent(Timestamp receiveTime);

    // ---- 回调设置 ----

    /// 设置读事件回调（收到 POLLIN|POLLPRI|POLLRDHUP 时调用）
    /// @note 回调签名同 TcpConnection::handleRead: void(Timestamp)
    void setReadCallback(ReadEventCallback cb) noexcept {
        readCallback_ = std::move(cb);
    }

    /// 设置写事件回调（收到 POLLOUT 时调用）
    void setWriteCallback(EventCallback cb) noexcept {
        writeCallback_ = std::move(cb);
    }

    /// 设置挂起事件回调（收到 POLLHUP 时调用）
    void setCloseCallback(EventCallback cb) noexcept {
        closeCallback_ = std::move(cb);
    }

    /// 设置错误事件回调（收到 POLLERR|POLLNVAL 时调用）
    void setErrorCallback(EventCallback cb) noexcept {
        errorCallback_ = std::move(cb);
    }

    /// Tie this channel to the owner object managed by shared_ptr,
    /// prevent the owner object being destroyed in handleEvent.
    /// @brief 将 Channel 与 owner（如 TcpConnection）绑定
    ///
    /// tie 机制：handleEvent 前会 lock 这个 weak_ptr，
    /// 如果 owner 已被销毁则跳过回调（防止 use-after-free）。
    ///
    void tie(const std::shared_ptr<void>&) noexcept;

    // ---- 访问器 ----

    [[nodiscard]] SocketHandle fd() const noexcept { return fd_; }

    /// 当前关注的事件类型
    [[nodiscard]] int events() const noexcept { return events_; }

    /// 设置 poll 返回的实际事件类型（由 Poller 调用）
    void set_revents(int revt) noexcept { revents_ = revt; }  // used by pollers

    /// 是否不关注任何事件
    bool isNoneEvent() const noexcept { return events_ == kNoneEvent; }

    // ---- 事件注册开关 ----

    /// 启用读事件监听（注册到 Poller）
    void enableReading() noexcept {
        events_ |= kReadEvent;
        update();
    }

    /// 禁用读事件监听
    void disableReading() noexcept {
        events_ &= ~kReadEvent;
        update();
    }

    /// 启用写事件监听
    void enableWriting() noexcept {
        events_ |= kWriteEvent;
        update();
    }

    /// 禁用写事件监听
    void disableWriting() noexcept {
        events_ &= ~kWriteEvent;
        update();
    }

    /// 禁用所有事件监听
    void disableAll() noexcept {
        events_ = kNoneEvent;
        update();
    }

    bool isWriting() const noexcept { return events_ & kWriteEvent; }
    bool isReading() const noexcept { return events_ & kReadEvent; }

    // for Poller
    int index() const noexcept { return index_; }
    void set_index(int idx) noexcept { index_ = idx; }

    // for debug
    std::string reventsToString() const noexcept;
    std::string eventsToString() const noexcept;

    void doNotLogHup() noexcept { logHup_ = false; }

    EventLoop* ownerLoop() noexcept { return loop_; }

    /// @brief 从 Poller 中移除（析构前必须调用）
    /// 前置条件：events == 0（应已调用 disableAll）
    void remove() noexcept;

private:
    static const int kNoneEvent;
    const static int kReadEvent;
    const static int kWriteEvent;

    /// 通知 Poller 更新事件注册（由 enable/disable 调用，标记 addedToLoop_）
    void update() noexcept;

    /// 带 guard 的实际事件处理（区分是否已 tie）
    void handleEventWithGuard(Timestamp receiveTime) noexcept;

    EventLoop* loop_;    ///< 所属的 EventLoop（非空）
    const SocketHandle fd_;       ///< 监控的文件描述符

    int events_{};       ///< 关注的事件（POLLIN / POLLOUT / ...）
    int revents_{};      ///< 实际发生的事件（由 Poller 填充）
    int index_{-1};      ///< Poller 索引（PollPoller 为数组下标，EPollPoller 为 kNew/kAdded/kDeleted）
    bool logHup_{true};  ///< 是否记录 POLLHUP 日志

    std::weak_ptr<void> tie_;  ///< owner 对象的弱引用（防 use-after-free）
    bool tied_{false};         ///< 是否已 tie
    bool eventHandling_{false};///< 是否正在处理事件（析构时断言）
    bool addedToLoop_{false};  ///< 是否已加入 Poller（析构时断言）

    ReadEventCallback readCallback_;    ///< 可读回调（含时间戳）
    EventCallback writeCallback_;       ///< 可写回调
    EventCallback closeCallback_;       ///< 挂起回调（POLLHUP）
    EventCallback errorCallback_;       ///< 错误回调（POLLERR）
};
}  // namespace chaoxi::net
