#pragma once

#include "chaoxi/net/EventLoop.hpp"

#include <unordered_map>

namespace chaoxi::net
{
class Channel;

///
/// Base class for IO Multiplexing
///
/// This class doesn't own the Channel objects.
class Poller
{
public:
    using FDType = SocketHandle;
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop* loop);
    virtual ~Poller() = default;
    Poller(const Poller&) = delete;
    Poller(Poller&&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller& operator=(Poller&&) = delete;

    /// Polls the I/O events.
    /// Must be called in the loop thread.
    [[nodiscard]] virtual Timestamp poll(int timeoutMs,
                                         ChannelList* activeChannels) = 0;

    /// Changes the interested I/O events.
    /// Must be called in the loop thread.
    virtual void updateChannel(Channel* channel) noexcept = 0;

    /// Remove the channel, when it destructs.
    /// Must be called in the loop thread.
    virtual void removeChannel(Channel* channel) noexcept = 0;

    [[nodiscard]] virtual bool hasChannel(Channel* channel) const noexcept;

    static Poller* newDefaultPoller(EventLoop* loop);

    void assertInLoopThread() const { ownerLoop_->assertInLoopThread(); }

protected:
    using ChannelMap = std::unordered_map<FDType, Channel*>;

    ChannelMap channels_;

private:
    EventLoop* ownerLoop_;
};
}  // namespace chaoxi::net
