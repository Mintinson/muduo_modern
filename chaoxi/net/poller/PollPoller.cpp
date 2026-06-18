#include "chaoxi/net/poller/PollPoller.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/Poller.hpp"

#include <cassert>
#include <cerrno>
#include <format>

#include <sys/poll.h>

namespace chaoxi::net {
PollPoller::PollPoller(EventLoop* loop) : Poller(loop) {}

void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const noexcept {
    for (auto& pfd : pollfds_) {
        if (pfd.revents > 0) {
            --numEvents;
            auto ch_it = channels_.find(pfd.fd);

            assert(ch_it != channels_.end());

            Channel* channel = ch_it->second;

            assert(channel->fd() == pfd.fd);

            channel->set_revents(pfd.revents);

            activeChannels->push_back(channel);
        }
    }
}

Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels) {
    int numEvents = ::poll(pollfds_.data(), pollfds_.size(), timeoutMs);

    int savedErrno = errno;

    Timestamp now = Timestamp::clock::now();

    if (numEvents > 0) {
        LOG_TRACE << std::format("{} events happened", numEvents);
        fillActiveChannels(numEvents, activeChannels);
    } else if (numEvents == 0) {
        LOG_TRACE << " nothing happened";
    } else {
        if (savedErrno != EINTR) {
            errno = savedErrno;
            LOG_SYSERR << "PollPoller::poll()";
        }
    }
    return now;
}

void PollPoller::updateChannel(Channel* channel) noexcept {
    Poller::assertInLoopThread();

    LOG_TRACE << std::format("fd = {} events = {}", channel->fd(),
                             channel->events());

    if (channel->index() < 0) {
        // a new one, add to pollfds_
        assert(!channels_.contains(channel->fd()));

        struct pollfd pfd;
        pfd.fd = channel->fd();
        pfd.events = static_cast<short>(channel->events());
        pfd.revents = 0;
        pollfds_.push_back(pfd);
        int idx = static_cast<int>(pollfds_.size()) - 1;
        channel->set_index(idx);
        channels_[pfd.fd] = channel;
    } else {
        assert(channels_.contains(channel->fd()));
        assert(channels_[channel->fd()] == channel);

        int idx = channel->index();
        assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));

        auto& pfd = pollfds_[idx];

        assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd() - 1);

        pfd.fd = channel->fd();
        pfd.events = static_cast<short>(channel->events());
        pfd.revents = 0;
        if (channel->isNoneEvent()) {
            // ignore this pollfd
            pfd.fd = -channel->fd() - 1;
        }
    }
}

void PollPoller::removeChannel(Channel* channel) noexcept {
    Poller::assertInLoopThread();
    LOG_TRACE << std::format("fd = {}", channel->fd());
    assert(channels_.contains(channel->fd()));
    assert(channels_[channel->fd()] == channel);
    assert(channel->isNoneEvent());

    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    const struct pollfd& pfd = pollfds_[idx];
    (void)pfd;
    assert(pfd.fd == -channel->fd() - 1 && pfd.events == channel->events());
    size_t n = channels_.erase(channel->fd());
    assert(n == 1);
    (void)n;
    if (static_cast<size_t>(idx) == pollfds_.size() - 1) {
        pollfds_.pop_back();
    } else {
        int channelAtEnd = pollfds_.back().fd;
        iter_swap(pollfds_.begin() + idx, pollfds_.end() - 1);
        if (channelAtEnd < 0) {
            channelAtEnd = -channelAtEnd - 1;
        }
        channels_[channelAtEnd]->set_index(idx);
        pollfds_.pop_back();
    }
}

}  // namespace chaoxi::net