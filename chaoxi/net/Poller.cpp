#include "chaoxi/net/Poller.hpp"
#include "chaoxi/net/Channel.hpp"

namespace chaoxi::net {
Poller::Poller(EventLoop* loop) : ownerLoop_(loop) {}

bool Poller::hasChannel(Channel* channel) const noexcept {
    assertInLoopThread();
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}
}  // namespace chaoxi::net