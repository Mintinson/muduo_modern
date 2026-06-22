#pragma once

#include "chaoxi/base/Timestamp.hpp"

#include <functional>
#include <memory>
#include <string>

namespace chaoxi::net
{

class EventLoop;

class Channel
{
public:
    // using ReadEventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp receiveTime)>;
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd) {}

    ~Channel();
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    Channel(Channel&&) noexcept = delete;
    Channel& operator=(Channel&&) noexcept = delete;

    void handleEvent(Timestamp receiveTime);

    void setReadCallback(ReadEventCallback cb) noexcept
    {
        readCallback_ = std::move(cb);
    }

    void setWriteCallback(EventCallback cb) noexcept
    {
        writeCallback_ = std::move(cb);
    }

    void setCloseCallback(EventCallback cb) noexcept
    {
        closeCallback_ = std::move(cb);
    }

    void setErrorCallback(EventCallback cb) noexcept
    {
        errorCallback_ = std::move(cb);
    }

    /// Tie this channel to the owner object managed by shared_ptr,
    /// prevent the owner object being destroyed in handleEvent.
    void tie(const std::shared_ptr<void>&) noexcept;

    [[nodiscard]] int fd() const noexcept { return fd_; }

    [[nodiscard]] int events() const noexcept { return events_; }

    void set_revents(int revt) noexcept { revents_ = revt; }  // used by pollers

    // int revents() const { return revents_; }
    bool isNoneEvent() const noexcept { return events_ == kNoneEvent; }

    void enableReading() noexcept
    {
        events_ |= kReadEvent;
        update();
    }

    void disableReading() noexcept
    {
        events_ &= ~kReadEvent;
        update();
    }

    void enableWriting() noexcept
    {
        events_ |= kWriteEvent;
        update();
    }

    void disableWriting() noexcept
    {
        events_ &= ~kWriteEvent;
        update();
    }

    void disableAll() noexcept
    {
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

    void remove() noexcept;

private:
    static const int kNoneEvent;
    const static int kReadEvent;
    const static int kWriteEvent;

    // static std::string eventsToString(int fd, int ev);
    void update() noexcept;
    void handleEventWithGuard(Timestamp receiveTime) noexcept;

    EventLoop* loop_;
    const int fd_;

    int events_{};
    int revents_{};  // it's the received event types of epoll or poll
    int index_{-1};  // used by Poller
    bool logHup_{true};

    std::weak_ptr<void> tie_;
    bool tied_{false};
    bool eventHandling_{false};
    bool addedToLoop_{false};
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};
}  // namespace chaoxi::net