///
/// @file DefaultPoller.cpp
/// @brief 根据平台选择最优的 Poller 实现
///

#include "chaoxi/net/Poller.hpp"

#if defined(__linux__)
    #include "chaoxi/net/poller/EPollPoller.hpp"
#else
    #include "chaoxi/net/poller/PollPoller.hpp"
#endif

namespace chaoxi::net
{

Poller* Poller::newDefaultPoller(EventLoop* loop)
{
#if defined(__linux__)
    return new EPollPoller(loop);
#else
    return new PollPoller(loop);
#endif
}

}  // namespace chaoxi::net
