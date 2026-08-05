#pragma once

#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/v2/AsyncSocket.hpp"
#include "chaoxi/v2/Task.hpp"

#include <memory>

namespace chaoxi::v2
{

struct AcceptedConnection
{
    AsyncSocket socket;
    net::InetAddress peerAddress;
};

class AsyncAcceptor
{
public:
    AsyncAcceptor(net::EventLoop& loop,
                  const net::InetAddress& listenAddress,
                  bool reusePort = false);
    ~AsyncAcceptor();

    AsyncAcceptor(AsyncAcceptor&& other) noexcept;
    AsyncAcceptor& operator=(AsyncAcceptor&& other) noexcept;

    AsyncAcceptor(const AsyncAcceptor&) = delete;
    AsyncAcceptor& operator=(const AsyncAcceptor&) = delete;

    [[nodiscard]] net::InetAddress localAddress() const;
    [[nodiscard]] bool isOpen() const noexcept;

    Task<AcceptedConnection> accept();
    void close();

private:
    struct State;

    static Task<AcceptedConnection> acceptImpl(std::shared_ptr<State> state);

    std::shared_ptr<State> state_;
};

}  // namespace chaoxi::v2
