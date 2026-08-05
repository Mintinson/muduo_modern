#include "chaoxi/net/poller/PollPoller.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/Poller.hpp"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <format>

#ifndef _WIN32
#include <sys/poll.h>
#endif

namespace chaoxi::net
{
PollPoller::PollPoller(EventLoop* loop) : Poller(loop) {}

void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const noexcept
{
    for (auto& pfd : pollfds_)
    {
        if (pfd.revents > 0)
        {
            --numEvents;
            auto ch_it = channels_.find(pfd.fd);

            assert(ch_it != channels_.end());

            Channel* channel = ch_it->second;

            assert(channel->fd() == pfd.fd);

            channel->set_revents(pfd.revents);

            activeChannels->push_back(channel);
        }
        if (numEvents == 0)
        {
            break;
        }
    }
}

Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
#ifdef _WIN32
    fd_set readSet;
    fd_set writeSet;
    fd_set errorSet;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    FD_ZERO(&errorSet);
    for (auto& pfd : pollfds_)
    {
        pfd.revents = 0;
        if (pfd.fd == INVALID_SOCKET)
        {
            continue;
        }
        if ((pfd.events & (POLLIN | POLLPRI)) != 0)
        {
            FD_SET(pfd.fd, &readSet);
        }
        if ((pfd.events & POLLOUT) != 0)
        {
            FD_SET(pfd.fd, &writeSet);
        }
        FD_SET(pfd.fd, &errorSet);
    }
    timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int numEvents = ::select(0, &readSet, &writeSet, &errorSet, &timeout);
    if (numEvents > 0)
    {
        int readyDescriptors = 0;
        for (auto& pfd : pollfds_)
        {
            if (pfd.fd == INVALID_SOCKET)
            {
                continue;
            }
            if (FD_ISSET(pfd.fd, &readSet)) pfd.revents |= POLLIN;
            if (FD_ISSET(pfd.fd, &writeSet)) pfd.revents |= POLLOUT;
            if (FD_ISSET(pfd.fd, &errorSet)) pfd.revents |= POLLERR;
            if (pfd.revents != 0) ++readyDescriptors;
        }
        numEvents = readyDescriptors;
    }
#else
    int numEvents = ::poll(pollfds_.data(), pollfds_.size(), timeoutMs);
#endif

    int savedErrno =
#ifdef _WIN32
        lastSocketError();
#else
        errno;
#endif

    Timestamp now = Timestamp::clock::now();

    if (numEvents > 0)
    {
        LOG_TRACE << std::format("{} events happened", numEvents);
        fillActiveChannels(numEvents, activeChannels);
    }
    else if (numEvents == 0)
    {
        LOG_TRACE << " nothing happened";
    }
    else
    {
        if (savedErrno !=
#ifdef _WIN32
            WSAEINTR
#else
            EINTR
#endif
        )
        {
#ifdef _WIN32
            LOG_ERROR << std::format("PollPoller::WSAPoll() error {}", savedErrno);
#else
            errno = savedErrno;
            LOG_SYSERR << "PollPoller::poll()";
#endif
        }
    }
    return now;
}

void PollPoller::updateChannel(Channel* channel) noexcept
{
    Poller::assertInLoopThread();

    LOG_TRACE << std::format("fd = {} events = {}", channel->fd(),
                             channel->events());

    if (channel->index() < 0)
    {
        // a new one, add to pollfds_
        assert(!channels_.contains(channel->fd()));

        ChaoxiPollFd pfd;
        pfd.fd = channel->fd();
        pfd.events = static_cast<short>(channel->events());
        pfd.revents = 0;
        pollfds_.push_back(pfd);
        int idx = static_cast<int>(pollfds_.size()) - 1;
        channel->set_index(idx);
        channels_[pfd.fd] = channel;
    }
    else
    {
        assert(channels_.contains(channel->fd()));
        assert(channels_[channel->fd()] == channel);

        int idx = channel->index();
        assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));

        auto& pfd = pollfds_[(std::size_t)idx];

#ifndef _WIN32
        assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd() - 1);
#else
        assert(pfd.fd == channel->fd() || pfd.fd == INVALID_SOCKET);
#endif

        pfd.fd = channel->fd();
        pfd.events = static_cast<short>(channel->events());
        pfd.revents = 0;
        if (channel->isNoneEvent())
        {
            // ignore this pollfd
#ifdef _WIN32
            pfd.fd = INVALID_SOCKET;
#else
            pfd.fd = -channel->fd() - 1;
#endif
        }
    }
}

void PollPoller::removeChannel(Channel* channel) noexcept
{
    Poller::assertInLoopThread();
    LOG_TRACE << std::format("fd = {}", channel->fd());
    assert(channels_.contains(channel->fd()));
    assert(channels_[channel->fd()] == channel);
    assert(channel->isNoneEvent());

    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    const ChaoxiPollFd& pfd = pollfds_[(std::size_t)idx];
    (void)pfd;
#ifdef _WIN32
    assert(pfd.fd == INVALID_SOCKET && pfd.events == channel->events());
#else
    assert(pfd.fd == -channel->fd() - 1 && pfd.events == channel->events());
#endif
    size_t n = channels_.erase(channel->fd());
    assert(n == 1);
    (void)n;
    if (static_cast<size_t>(idx) == pollfds_.size() - 1)
    {
        pollfds_.pop_back();
    }
    else
    {
        SocketHandle channelAtEnd = pollfds_.back().fd;
        iter_swap(pollfds_.begin() + idx, pollfds_.end() - 1);
#ifdef _WIN32
        if (channelAtEnd == INVALID_SOCKET)
        {
            for (const auto& [fd, candidate] : channels_)
            {
                if (candidate->index() == static_cast<int>(pollfds_.size()) - 1)
                {
                    channelAtEnd = fd;
                    break;
                }
            }
        }
#else
        if (channelAtEnd < 0)
        {
            channelAtEnd = -channelAtEnd - 1;
        }
#endif
        channels_[channelAtEnd]->set_index(idx);
        pollfds_.pop_back();
    }
}

}  // namespace chaoxi::net
