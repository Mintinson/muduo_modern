#include "chaoxi/v2/AsyncAcceptor.hpp"

#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <atomic>
#include <cerrno>
#include <coroutine>
#include <deque>
#include <optional>
#include <system_error>
#include <utility>

namespace chaoxi::v2
{
struct AsyncAcceptor::State : std::enable_shared_from_this<State>
{
    struct PendingConnection
    {
        net::SocketHandle fd;
        net::InetAddress peerAddress;
    };

    struct Waiter
    {
        std::coroutine_handle<> coroutine{};
        std::error_code error;
        std::optional<PendingConnection> connection;
    };

    class Awaiter
    {
    public:
        explicit Awaiter(std::shared_ptr<State> state) noexcept
            : state_(std::move(state))
        {
        }

        ~Awaiter()
        {
            if (waiter_.coroutine && state_->waiter_ == &waiter_)
            {
                state_->waiter_ = nullptr;
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

            if (!state_->pending_.empty())
            {
                waiter_.connection = std::move(state_->pending_.front());
                state_->pending_.pop_front();
                waiter_.coroutine = {};
                return false;
            }

            if (state_->waiter_ != nullptr)
            {
                waiter_.coroutine = {};
                waiter_.error =
                    std::make_error_code(std::errc::operation_in_progress);
                return false;
            }

            state_->waiter_ = &waiter_;
            return true;
        }

        AcceptedConnection await_resume()
        {
            if (waiter_.error)
            {
                throw std::system_error(waiter_.error);
            }
            if (!waiter_.connection)
            {
                throw std::system_error(
                    std::make_error_code(std::errc::io_error));
            }

            auto connection = std::move(*waiter_.connection);
            return AcceptedConnection{
                AsyncSocket{state_->loop_, connection.fd},
                std::move(connection.peerAddress)
            };
        }

    private:
        std::shared_ptr<State> state_;
        Waiter waiter_;
    };

    State(net::EventLoop& loop,
          const net::InetAddress& listenAddress,
          bool reusePort)
        : loop_(loop)
        , fd_(net::sockets::createNonblocking(listenAddress.family()))
        , channel_(&loop, fd_.load(std::memory_order_relaxed))
    {
        loop_.assertInLoopThread();
        const net::SocketHandle fd = fd_.load(std::memory_order_relaxed);
        if (fd == net::kInvalidSocket)
        {
            throw std::system_error(errno, std::generic_category());
        }

        const int enabled = 1;
        if (net::sockets::setSocketOption(fd, SOL_SOCKET, SO_REUSEADDR,
                                          enabled) < 0)
        {
            const int error = errno;
            net::sockets::close(fd);
            fd_.store(net::kInvalidSocket, std::memory_order_relaxed);
            throw std::system_error(error, std::generic_category());
        }
#ifdef SO_REUSEPORT
        if (reusePort && net::sockets::setSocketOption(
                             fd, SOL_SOCKET, SO_REUSEPORT, enabled) < 0)
        {
            const int error = errno;
            net::sockets::close(fd);
            fd_.store(net::kInvalidSocket, std::memory_order_relaxed);
            throw std::system_error(error, std::generic_category());
        }
#else
        if (reusePort)
        {
            net::sockets::close(fd);
            fd_.store(net::kInvalidSocket, std::memory_order_relaxed);
            throw std::system_error(
                std::make_error_code(std::errc::operation_not_supported));
        }
#endif
        if (net::sockets::bind(fd, listenAddress.getSockAddr()) < 0 ||
            net::sockets::listen(fd) < 0)
        {
            const int error = errno;
            net::sockets::close(fd);
            fd_.store(net::kInvalidSocket, std::memory_order_relaxed);
            throw std::system_error(error, std::generic_category());
        }
    }

    static std::shared_ptr<State> create(net::EventLoop& loop,
                                         const net::InetAddress& listenAddress,
                                         bool reusePort)
    {
        auto state =
            std::shared_ptr<State>(new State(loop, listenAddress, reusePort));
        state->initialize();
        return state;
    }

    void initialize()
    {
        std::weak_ptr<State> weakState = shared_from_this();
        channel_.tie(shared_from_this());
        channel_.setReadCallback(
            [weakState](Timestamp)
            {
                if (auto state = weakState.lock())
                {
                    state->handleAccept();
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
                    state->completeError(
                        std::error_code(error, std::generic_category()));
                }
            });
        channel_.enableReading();
        registered_ = true;
    }

    void handleAccept()
    {
        loop_.assertInLoopThread();
        while (true)
        {
            sockaddr_in6 peer{};
            const net::SocketHandle connectionFd = net::sockets::accept(
                fd_.load(std::memory_order_acquire), &peer);
            if (connectionFd != net::kInvalidSocket)
            {
                deliver(PendingConnection{connectionFd, net::InetAddress{peer}});
                continue;
            }
            if (errno == EINTR || errno == ECONNABORTED)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }
            completeError(std::error_code(errno, std::generic_category()));
            return;
        }
    }

    void deliver(PendingConnection connection)
    {
        if (waiter_ == nullptr)
        {
            pending_.push_back(std::move(connection));
            return;
        }

        Waiter* waiter = std::exchange(waiter_, nullptr);
        waiter->connection = std::move(connection);
        auto coroutine = std::exchange(waiter->coroutine, {});
        loop_.queueInLoop(
            [coroutine]
            {
                if (coroutine && !coroutine.done())
                {
                    coroutine.resume();
                }
            });
    }

    void completeError(std::error_code error)
    {
        if (waiter_ == nullptr)
        {
            return;
        }

        Waiter* waiter = std::exchange(waiter_, nullptr);
        waiter->error = error;
        auto coroutine = std::exchange(waiter->coroutine, {});
        loop_.queueInLoop(
            [coroutine]
            {
                if (coroutine && !coroutine.done())
                {
                    coroutine.resume();
                }
            });
    }

    void closeInLoop()
    {
        loop_.assertInLoopThread();
        if (closed_.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        completeError(std::make_error_code(std::errc::operation_canceled));
        for (auto& connection : pending_)
        {
            net::sockets::close(connection.fd);
        }
        pending_.clear();

        if (registered_)
        {
            channel_.disableAll();
            channel_.remove();
            registered_ = false;
        }

        const net::SocketHandle fd =
            fd_.exchange(net::kInvalidSocket, std::memory_order_acq_rel);
        if (fd != net::kInvalidSocket)
        {
            net::sockets::close(fd);
        }
    }

    net::EventLoop& loop_;
    std::atomic<net::SocketHandle> fd_;
    net::Channel channel_;
    std::atomic_bool closed_{false};
    bool registered_ = false;
    std::deque<PendingConnection> pending_;
    Waiter* waiter_ = nullptr;
};

AsyncAcceptor::AsyncAcceptor(net::EventLoop& loop,
                             const net::InetAddress& listenAddress,
                             bool reusePort)
    : state_(State::create(loop, listenAddress, reusePort))
{
}

AsyncAcceptor::~AsyncAcceptor()
{
    close();
}

AsyncAcceptor::AsyncAcceptor(AsyncAcceptor&& other) noexcept
    : state_(std::move(other.state_))
{
}

AsyncAcceptor& AsyncAcceptor::operator=(AsyncAcceptor&& other) noexcept
{
    if (this != &other)
    {
        close();
        state_ = std::move(other.state_);
    }
    return *this;
}

net::InetAddress AsyncAcceptor::localAddress() const
{
    if (!state_ || state_->closed_.load(std::memory_order_acquire))
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    return net::InetAddress{
        net::sockets::getLocalAddr(state_->fd_.load(std::memory_order_acquire))};
}

bool AsyncAcceptor::isOpen() const noexcept
{
    return state_ && !state_->closed_.load(std::memory_order_acquire);
}

Task<AcceptedConnection> AsyncAcceptor::accept()
{
    return acceptImpl(state_);
}

Task<AcceptedConnection> AsyncAcceptor::acceptImpl(std::shared_ptr<State> state)
{
    if (!state)
    {
        throw std::system_error(
            std::make_error_code(std::errc::bad_file_descriptor));
    }
    co_return co_await State::Awaiter{std::move(state)};
}

void AsyncAcceptor::close()
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
