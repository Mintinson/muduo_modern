#include "chaoxi/v2/AsyncFd.hpp"

#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <atomic>
#include <cerrno>
#include <coroutine>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace chaoxi::v2
{

struct AsyncFd::State : std::enable_shared_from_this<State>
{
    enum class Direction
    {
        read,
        write,
    };

    struct Waiter
    {
        std::coroutine_handle<> coroutine{};
        std::error_code error;
    };

    class Awaiter
    {
    public:
        Awaiter(std::shared_ptr<State> state, Direction direction) noexcept
            : state_(std::move(state))
            , direction_(direction)
        {
        }

        ~Awaiter()
        {
            if (waiter_.coroutine)
            {
                state_->cancelWaiter(direction_, &waiter_);
            }
        }

        [[nodiscard]] bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> coroutine)
        {
            state_->loop_.assertInLoopThread();
            waiter_.coroutine = coroutine;

            if (state_->closed_.load(std::memory_order_acquire))
            {
                waiter_.coroutine = {};
                waiter_.error =
                    std::make_error_code(std::errc::bad_file_descriptor);
                return false;
            }

            Waiter*& slot = state_->waiter(direction_);
            if (slot != nullptr)
            {
                waiter_.coroutine = {};
                waiter_.error =
                    std::make_error_code(std::errc::operation_in_progress);
                return false;
            }

            slot = &waiter_;
            state_->enable(direction_);
            return true;
        }

        void await_resume()
        {
            if (waiter_.error)
            {
                throw std::system_error(waiter_.error);
            }
        }

    private:
        std::shared_ptr<State> state_;
        Direction direction_;
        Waiter waiter_;
    };

    State(net::EventLoop& loop, int fd, FdOwnership ownership)
        : loop_(loop)
        , fd_(fd)
        , ownership_(ownership)
        , channel_(&loop, fd)
    {
        if (fd < 0)
        {
            throw std::system_error(
                std::make_error_code(std::errc::bad_file_descriptor));
        }
        loop_.assertInLoopThread();
    }

    static std::shared_ptr<State> create(net::EventLoop& loop,
                                         int fd,
                                         FdOwnership ownership)
    {
        auto state = std::shared_ptr<State>(new State(loop, fd, ownership));
        state->initializeCallbacks();
        return state;
    }

    void initializeCallbacks()
    {
        std::weak_ptr<State> weakState = shared_from_this();
        channel_.tie(shared_from_this());
        channel_.setReadCallback(
            [weakState](Timestamp)
            {
                if (auto state = weakState.lock())
                {
                    state->complete(Direction::read, {});
                }
            });
        channel_.setWriteCallback(
            [weakState]
            {
                if (auto state = weakState.lock())
                {
                    state->complete(Direction::write, {});
                }
            });
        channel_.setCloseCallback(
            [weakState]
            {
                if (auto state = weakState.lock())
                {
                    const int error = net::sockets::getSocketError(
                        state->fd_.load(std::memory_order_acquire));
                    if (error != 0)
                    {
                        const std::error_code code(error,
                                                   std::system_category());
                        state->complete(Direction::read, code);
                        state->complete(Direction::write, code);
                    }
                    else
                    {
                        state->complete(Direction::read, {});
                        state->complete(
                            Direction::write,
                            std::make_error_code(std::errc::broken_pipe));
                    }
                }
            });
        channel_.setErrorCallback(
            [weakState]
            {
                if (auto state = weakState.lock())
                {
                    int error = net::sockets::getSocketError(
                        state->fd_.load(std::memory_order_acquire));
                    if (error == 0)
                    {
                        error = EIO;
                    }
                    const std::error_code code(error, std::system_category());
                    state->complete(Direction::read, code);
                    state->complete(Direction::write, code);
                }
            });
    }

    [[nodiscard]] Waiter*& waiter(Direction direction) noexcept
    {
        return direction == Direction::read ? readWaiter_ : writeWaiter_;
    }

    void enable(Direction direction)
    {
        if (direction == Direction::read)
        {
            if (!channel_.isReading())
            {
                channel_.enableReading();
                registered_ = true;
            }
        }
        else if (!channel_.isWriting())
        {
            channel_.enableWriting();
            registered_ = true;
        }
    }

    void disable(Direction direction)
    {
        if (direction == Direction::read)
        {
            if (channel_.isReading())
            {
                channel_.disableReading();
            }
        }
        else if (channel_.isWriting())
        {
            channel_.disableWriting();
        }
    }

    void complete(Direction direction, std::error_code error)
    {
        loop_.assertInLoopThread();
        Waiter*& slot = waiter(direction);
        if (slot == nullptr)
        {
            return;
        }

        Waiter* completed = std::exchange(slot, nullptr);
        disable(direction);
        completed->error = error;
        auto coroutine = std::exchange(completed->coroutine, {});
        loop_.queueInLoop(
            [coroutine]
            {
                if (coroutine && !coroutine.done())
                {
                    coroutine.resume();
                }
            });
    }

    void cancelWaiter(Direction direction, Waiter* candidate)
    {
        loop_.assertInLoopThread();
        Waiter*& slot = waiter(direction);
        if (slot == candidate)
        {
            slot = nullptr;
            disable(direction);
        }
        candidate->coroutine = {};
    }

    void closeInLoop()
    {
        loop_.assertInLoopThread();
        if (closed_.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        const auto cancelled =
            std::make_error_code(std::errc::operation_canceled);
        complete(Direction::read, cancelled);
        complete(Direction::write, cancelled);

        if (registered_)
        {
            if (!channel_.isNoneEvent())
            {
                channel_.disableAll();
            }
            channel_.remove();
            registered_ = false;
        }

        const int fd = fd_.exchange(-1, std::memory_order_acq_rel);
        if (ownership_ == FdOwnership::owned && fd >= 0)
        {
            net::sockets::close(fd);
        }
    }

    net::EventLoop& loop_;
    std::atomic_int fd_;
    FdOwnership ownership_;
    net::Channel channel_;
    std::atomic_bool closed_{false};
    bool registered_ = false;
    Waiter* readWaiter_ = nullptr;
    Waiter* writeWaiter_ = nullptr;
};

AsyncFd::AsyncFd(net::EventLoop& loop, int fd, FdOwnership ownership)
    : state_(State::create(loop, fd, ownership))
{
}

AsyncFd::AsyncFd(std::shared_ptr<State> state) noexcept
    : state_(std::move(state))
{
}

AsyncFd::~AsyncFd()
{
    close();
}

AsyncFd::AsyncFd(AsyncFd&& other) noexcept : state_(std::move(other.state_)) {}

AsyncFd& AsyncFd::operator=(AsyncFd&& other) noexcept
{
    if (this != &other)
    {
        close();
        state_ = std::move(other.state_);
    }
    return *this;
}

int AsyncFd::nativeHandle() const noexcept
{
    return state_ ? state_->fd_.load(std::memory_order_acquire) : -1;
}

bool AsyncFd::isOpen() const noexcept
{
    return state_ && !state_->closed_.load(std::memory_order_acquire);
}

net::EventLoop& AsyncFd::eventLoop() const
{
    if (!state_)
    {
        throw std::logic_error("AsyncFd has no EventLoop");
    }
    return state_->loop_;
}

Task<void> AsyncFd::waitReadable()
{
    return waitReadableImpl(state_);
}

Task<void> AsyncFd::waitWritable()
{
    return waitWritableImpl(state_);
}

Task<std::size_t> AsyncFd::readSome(std::span<std::byte> buffer)
{
    return readSomeImpl(state_, buffer);
}

Task<void> AsyncFd::readExactly(std::span<std::byte> buffer)
{
    return readExactlyImpl(state_, buffer);
}

Task<std::size_t> AsyncFd::writeSome(std::span<const std::byte> buffer)
{
    return writeSomeImpl(state_, buffer);
}

Task<void> AsyncFd::writeAll(std::span<const std::byte> buffer)
{
    return writeAllImpl(state_, buffer);
}

Task<void> AsyncFd::waitReadableImpl(std::shared_ptr<State> state)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    co_await State::Awaiter{std::move(state), State::Direction::read};
}

Task<void> AsyncFd::waitWritableImpl(std::shared_ptr<State> state)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    co_await State::Awaiter{std::move(state), State::Direction::write};
}

Task<std::size_t> AsyncFd::readSomeImpl(std::shared_ptr<State> state,
                                        std::span<std::byte> buffer)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    state->loop_.assertInLoopThread();
    if (buffer.empty())
    {
        co_return 0;
    }

    while (true)
    {
        const int fd = state->fd_.load(std::memory_order_acquire);
        const ssize_t result = ::read(fd, buffer.data(), buffer.size());
        if (result >= 0)
        {
            co_return static_cast<std::size_t>(result);
        }
        if (errno == EINTR)
        {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            co_await State::Awaiter{state, State::Direction::read};
            continue;
        }
        throw std::system_error(errno, std::system_category());
    }
}

Task<std::size_t> AsyncFd::writeSomeImpl(std::shared_ptr<State> state,
                                         std::span<const std::byte> buffer)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    state->loop_.assertInLoopThread();
    if (buffer.empty())
    {
        co_return 0;
    }

    while (true)
    {
        const int fd = state->fd_.load(std::memory_order_acquire);
        const ssize_t result = ::write(fd, buffer.data(), buffer.size());
        if (result >= 0)
        {
            co_return static_cast<std::size_t>(result);
        }
        if (errno == EINTR)
        {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            co_await State::Awaiter{state, State::Direction::write};
            continue;
        }
        throw std::system_error(errno, std::system_category());
    }
}

Task<void> AsyncFd::readExactlyImpl(std::shared_ptr<State> state,
                                    std::span<std::byte> buffer)
{
    std::size_t read = 0;
    while (read < buffer.size())
    {
        const std::size_t count =
            co_await readSomeImpl(state, buffer.subspan(read));
        if (count == 0)
        {
            throw std::system_error(
                std::make_error_code(std::errc::connection_reset));
        }
        read += count;
    }
}

Task<void> AsyncFd::writeAllImpl(std::shared_ptr<State> state,
                                 std::span<const std::byte> buffer)
{
    std::size_t written = 0;
    while (written < buffer.size())
    {
        const std::size_t count =
            co_await writeSomeImpl(state, buffer.subspan(written));
        if (count == 0)
        {
            throw std::system_error(std::make_error_code(std::errc::io_error));
        }
        written += count;
    }
}

void AsyncFd::close()
{
    auto state = std::exchange(state_, {});
    if (!state)
    {
        return;
    }

    if (state->loop_.isInLoopThread())
    {
        state->closeInLoop();
    }
    else
    {
        net::EventLoop* loop = &state->loop_;
        loop->queueInLoop([state = std::move(state)] { state->closeInLoop(); });
        loop->wakeup();
    }
}

}  // namespace chaoxi::v2
