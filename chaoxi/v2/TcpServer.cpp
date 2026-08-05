#include "chaoxi/v2/TcpServer.hpp"

#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/v2/AsyncAcceptor.hpp"
#include "chaoxi/v2/Spawn.hpp"

#include <atomic>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace chaoxi::v2
{

struct TcpServer::State
{
    State(net::EventLoop& eventLoop,
          const net::InetAddress& listenAddress,
          ConnectionHandler handler,
          bool reusePort)
        : loop(eventLoop)
        , acceptor(eventLoop, listenAddress, reusePort)
        , connectionHandler(std::move(handler))
    {
        if (!connectionHandler)
        {
            throw std::invalid_argument(
                "TcpServer requires a connection handler");
        }
    }

    net::EventLoop& loop;
    AsyncAcceptor acceptor;
    ConnectionHandler connectionHandler;
    ErrorHandler errorHandler;
    std::atomic_bool running{false};
    std::atomic_bool stopping{false};
};

TcpServer::TcpServer(net::EventLoop& loop,
                     const net::InetAddress& listenAddress,
                     ConnectionHandler connectionHandler,
                     bool reusePort)
    : state_(std::make_shared<State>(
          loop, listenAddress, std::move(connectionHandler), reusePort))
{
}

TcpServer::~TcpServer()
{
    stop();
}

TcpServer::TcpServer(TcpServer&& other) noexcept
    : state_(std::move(other.state_))
{
}

TcpServer& TcpServer::operator=(TcpServer&& other) noexcept
{
    if (this != &other)
    {
        stop();
        state_ = std::move(other.state_);
    }
    return *this;
}

net::InetAddress TcpServer::localAddress() const
{
    if (!state_)
    {
        throw std::logic_error("TcpServer has no state");
    }
    return state_->acceptor.localAddress();
}

bool TcpServer::running() const noexcept
{
    return state_ && state_->running.load(std::memory_order_acquire);
}

void TcpServer::setErrorHandler(ErrorHandler errorHandler)
{
    if (!state_)
    {
        throw std::logic_error("TcpServer has no state");
    }
    state_->loop.assertInLoopThread();
    state_->errorHandler = std::move(errorHandler);
}

Task<void> TcpServer::run()
{
    return runImpl(state_);
}

Task<void> TcpServer::runImpl(std::shared_ptr<State> state)
{
    if (!state)
    {
        throw std::logic_error("TcpServer has no state");
    }

    bool expected = false;
    if (!state->running.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel))
    {
        throw std::system_error(
            std::make_error_code(std::errc::operation_in_progress));
    }

    struct RunningGuard
    {
        std::atomic_bool& running;

        ~RunningGuard() { running.store(false, std::memory_order_release); }
    } guard{state->running};

    while (!state->stopping.load(std::memory_order_acquire))
    {
        try
        {
            auto connection = co_await state->acceptor.accept();
            auto task = state->connectionHandler(
                std::move(connection.socket), std::move(connection.peerAddress));
            std::weak_ptr<State> weakState = state;
            spawn(state->loop, std::move(task),
                  [weakState](std::exception_ptr error)
                  {
                      if (auto locked = weakState.lock();
                          locked && locked->errorHandler)
                      {
                          locked->errorHandler(error);
                          return;
                      }
                      std::terminate();
                  });
        }
        catch (const std::system_error& error)
        {
            if (state->stopping.load(std::memory_order_acquire) &&
                error.code() ==
                    std::make_error_code(std::errc::operation_canceled))
            {
                co_return;
            }
            throw;
        }
    }
}

void TcpServer::stop()
{
    auto state = state_;
    if (!state)
    {
        return;
    }
    state->stopping.store(true, std::memory_order_release);
    state->acceptor.close();
}

}  // namespace chaoxi::v2
