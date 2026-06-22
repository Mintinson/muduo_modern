///
/// @file DefaultPoller.cpp
/// @brief 根据平台选择最优的 Poller 实现
///

#include "chaoxi/net/Poller.hpp"
#include "chaoxi/net/poller/EPollPoller.hpp"

namespace chaoxi::net
{

Poller* Poller::newDefaultPoller(EventLoop* loop)
{
    // Linux 上 epoll(7) 是最高效的 I/O 多路复用机制
    return new EPollPoller(loop);
}

}  // namespace chaoxi::net
