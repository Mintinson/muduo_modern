///
/// @file Channel.cpp
/// @brief Channel 实现 —— 事件分发、回调调用、生命周期管理
///

#include "chaoxi/net/Channel.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <cassert>

#ifdef _WIN32
#include "chaoxi/net/Platform.hpp"
#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif
#else
#include <poll.h>
#endif

namespace chaoxi::net {

namespace {

/// 将 poll 事件标志转换为可读字符串（调试用）
std::string eventsToString(SocketHandle fd, int ev) noexcept {
    std::ostringstream oss;
    oss << fd << ": ";
    if (ev & POLLIN)     oss << "IN ";
    if (ev & POLLPRI)    oss << "PRI ";
    if (ev & POLLOUT)    oss << "OUT ";
    if (ev & POLLHUP)    oss << "HUP ";
    if (ev & POLLRDHUP)  oss << "RDHUP ";
    if (ev & POLLERR)    oss << "ERR ";
    if (ev & POLLNVAL)   oss << "NVAL ";
    return oss.str();
}
}  // namespace

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

///
/// @brief 析构 —— 确保 Channel 已从 EventLoop 中安全移除
///
/// 三个断言：
///   1. !eventHandling_  — 不能在事件分发中析构
///   2. !addedToLoop_    — 析构前必须调用 remove() 从 Poller 移除
///   3. !hasChannel(this) — 确认 Poller 中已无此 Channel（仅限同一线程）
///
Channel::~Channel() {
    assert(!eventHandling_);
    assert(!addedToLoop_);
    if (loop_->isInLoopThread()) {
        assert(!loop_->hasChannel(this));
    }
}

///
/// @brief 将 Channel 与 owner 对象的 shared_ptr 绑定
///
/// tie 机制解决了 Channel 回调时 owner（如 TcpConnection）已被销毁的问题：
/// Channel 调用 handleEvent 前会尝试 lock tie_（weak_ptr），
/// 如果 lock 成功说明 owner 还活着，安全执行回调；
/// 如果 lock 失败则跳过回调。
///
void Channel::tie(const std::shared_ptr<void>& obj) noexcept {
    tie_ = obj;
    tied_ = true;
}

///
/// @brief 向 EventLoop 发起事件更新请求
///
/// 由 enableReading/disableReading/enableWriting/disableWriting/disableAll
/// 在修改 events_ 后调用。update 将 addedToLoop_ 设为 true
/// 并委托 EventLoop → Poller 执行实际的 epoll_ctl 操作。
///
void Channel::update() noexcept {
    addedToLoop_ = true;
    loop_->updateChannel(this);
}

///
/// @brief 从 EventLoop 的 Poller 中移除 Channel
///
/// 析构前必须调用。前置条件：events_ == 0（即已 disableAll）。
/// 调用后 addedToLoop_ = false，满足析构断言。
///
void Channel::remove() noexcept {
    assert(isNoneEvent());
    addedToLoop_ = false;
    loop_->removeChannel(this);
}

///
/// @brief 事件总入口 —— 由 EventLoop 在 poll 返回后调用
///
/// 如果 Channel 绑定了 owner（tied_ = true），先尝试 lock weak_ptr，
/// lock 失败说明 owner 已销毁，跳过回调。
/// 否则直接调用 handleEventWithGuard。
///
void Channel::handleEvent(Timestamp receiveTime) {
    std::shared_ptr<void> guard;
    if (tied_) {
        guard = tie_.lock();
        if (guard) {
            handleEventWithGuard(receiveTime);
        }
    } else {
        handleEventWithGuard(receiveTime);
    }
}

///
/// @brief 实际的事件分发 —— 根据 revents_ 调用对应的回调
///
/// 回调分类优先级：
///   1. POLLHUP （对端关闭，无数据可读时）
///   2. POLLNVAL（fd 未打开）
///   3. POLLERR | POLLNVAL（错误）
///   4. POLLIN | POLLPRI | POLLRDHUP（可读）
///   5. POLLOUT（可写）
///
/// 执行期间 eventHandling_ = true，防止析构。
///
void Channel::handleEventWithGuard(Timestamp receiveTime) noexcept {
    eventHandling_ = true;
    LOG_TRACE << reventsToString();

    // POLLHUP: 对端关闭连接（且没有可读数据时触发 closeCallback）
    if ((revents_ & POLLHUP) && !(revents_ & POLLIN)) {
        if (logHup_) {
            LOG_WARN << std::format("fd = {} Channel::handle_event() POLLHUP",
                                     fd_);
        }
        if (closeCallback_) {
            closeCallback_();
        }
    }

    // POLLNVAL: fd 未打开
    if (revents_ & POLLNVAL) {
        LOG_WARN << std::format("fd = {} Channel::handle_event() POLLNVAL", fd_);
    }

    // 错误事件
    if (revents_ & (POLLERR | POLLNVAL)) {
        if (errorCallback_) {
            errorCallback_();
        }
    }

    // 可读事件 —— 最常见的路径
    if (revents_ & (POLLIN | POLLPRI | POLLRDHUP)) {
        if (readCallback_) {
            readCallback_(receiveTime);
        }
    }

    // 可写事件
    if (revents_ & POLLOUT) {
        if (writeCallback_) {
            writeCallback_();
        }
    }

    eventHandling_ = false;
}

/// 将 revents_ 格式化为字符串
std::string Channel::reventsToString() const noexcept {
    return ::chaoxi::net::eventsToString(fd_, revents_);
}

/// 将 events_ 格式化为字符串
std::string Channel::eventsToString() const noexcept {
    return ::chaoxi::net::eventsToString(fd_, events_);
}

}  // namespace chaoxi::net
