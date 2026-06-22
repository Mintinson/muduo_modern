#include "chaoxi/net/Channel.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <cassert>

#include <poll.h>

namespace chaoxi::net
{

namespace
{
std::string eventsToString(int fd, int ev) noexcept
{
    std::ostringstream oss;
    oss << fd << ": ";
    if (ev & POLLIN)
    {
        oss << "IN ";
    }
    if (ev & POLLPRI)
    {
        oss << "PRI ";
    }
    if (ev & POLLOUT)
    {
        oss << "OUT ";
    }
    if (ev & POLLHUP)
    {
        oss << "HUP ";
    }
    if (ev & POLLRDHUP)
    {
        oss << "RDHUP ";
    }
    if (ev & POLLERR)
    {
        oss << "ERR ";
    }
    if (ev & POLLNVAL)
    {
        oss << "NVAL ";
    }

    return oss.str();
}
}  // namespace

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::~Channel()
{
    assert(!eventHandling_);
    assert(!addedToLoop_);
    if (loop_->isInLoopThread())
    {
        assert(!loop_->hasChannel(this));
    }
}

void Channel::tie(const std::shared_ptr<void>& obj) noexcept
{
    tie_ = obj;
    tied_ = true;
}

void Channel::update() noexcept
{
    addedToLoop_ = true;
    loop_->updateChannel(this);
}

void Channel::remove() noexcept
{
    assert(isNoneEvent());
    addedToLoop_ = false;
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    std::shared_ptr<void> guard;
    if (tied_)
    {
        guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
    }
    else
    {
        handleEventWithGuard(receiveTime);
    }
}

void Channel::handleEventWithGuard(Timestamp receiveTime) noexcept
{
    eventHandling_ = true;
    LOG_TRACE << reventsToString();
    if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
    {
        if (logHup_)
        {
            LOG_WARN << std::format("fd = {} Channel::handle_event() POLLHUP",
                                    fd_);
        }
        if (closeCallback_)
        {
            closeCallback_();
        }
    }

    if (revents_ & POLLNVAL)
    {
        LOG_WARN << std::format("fd = {} Channel::handle_event() POLLNVAL", fd_);
    }

    if (revents_ & (POLLERR | POLLNVAL))
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }
    if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }
    if (revents_ & POLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
    eventHandling_ = false;
}

std::string Channel::reventsToString() const noexcept
{
    return ::chaoxi::net::eventsToString(fd_, revents_);
}

std::string Channel::eventsToString() const noexcept
{
    return ::chaoxi::net::eventsToString(fd_, events_);
}

}  // namespace chaoxi::net