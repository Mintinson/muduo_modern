#pragma once

#include "chaoxi/net/Poller.hpp"

#include <vector>

struct pollfd;

namespace chaoxi::net {
class PollPoller : public Poller {
public:
    PollPoller(EventLoop* loop);
    ~PollPoller() override = default;

    /// @brief
    /// 调用poll获得当前活动的IO事件，然后填充调用方传入的activeChannels，并返回poll
    /// return的时刻。
    Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;

    void updateChannel(Channel* channel) noexcept override;
    void removeChannel(Channel* channel) noexcept override;

private:
    /**
     * @brief
     * 遍历pollfds_，找出有活动事件的fd，把它对应的Channel填入activeChannels。
     *
     * @param numEvents
     * @param activeChannels
     */
    void fillActiveChannels(int numEvents,
                            ChannelList* activeChannels) const noexcept;
    using PollFdList = std::vector<struct pollfd>;
    PollFdList pollfds_;
};
}  // namespace chaoxi::net