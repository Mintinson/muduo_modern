#pragma once

#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/v2/AsyncSocket.hpp"
#include "chaoxi/v2/Task.hpp"

#include <exception>
#include <functional>
#include <memory>

namespace chaoxi::net
{
class EventLoop;
}  // namespace chaoxi::net

namespace chaoxi::v2
{

class TcpServer
{
public:
    using ConnectionHandler =
        std::move_only_function<Task<void>(AsyncSocket, net::InetAddress)>;
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    TcpServer(net::EventLoop& loop,
              const net::InetAddress& listenAddress,
              ConnectionHandler connectionHandler,
              bool reusePort = false);
    ~TcpServer();

    TcpServer(TcpServer&& other) noexcept;
    TcpServer& operator=(TcpServer&& other) noexcept;

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    [[nodiscard]] net::InetAddress localAddress() const;
    [[nodiscard]] bool running() const noexcept;

    void setErrorHandler(ErrorHandler errorHandler);

    Task<void> run();
    void stop();

private:
    struct State;

    static Task<void> runImpl(std::shared_ptr<State> state);

    std::shared_ptr<State> state_;
};

}  // namespace chaoxi::v2
