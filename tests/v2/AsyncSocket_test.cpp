#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/v2/AsyncAcceptor.hpp"
#include "chaoxi/v2/AsyncSocket.hpp"
#include "chaoxi/v2/Spawn.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

namespace
{

std::span<const std::byte> bytes(std::string_view value)
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

chaoxi::v2::Task<void> runServer(chaoxi::net::EventLoop& loop,
                                 chaoxi::v2::AsyncAcceptor& acceptor,
                                 int& completed)
{
    auto connection = co_await acceptor.accept();
    std::array<char, 4> request{};
    co_await connection.socket.readExactly(
        std::as_writable_bytes(std::span{request}));
    if (std::string_view{request} != "ping")
    {
        throw std::runtime_error("server received an invalid request");
    }

    co_await connection.socket.writeAll(bytes("pong"));
    connection.socket.close();
    ++completed;
    if (completed == 2)
    {
        loop.quit();
    }
}

chaoxi::v2::Task<void> runClient(chaoxi::net::EventLoop& loop,
                                 chaoxi::net::InetAddress address,
                                 std::string& response,
                                 int& completed)
{
    auto socket = co_await chaoxi::v2::AsyncSocket::connect(loop, address);
    socket.setTcpNoDelay(true);
    co_await socket.writeAll(bytes("ping"));

    std::array<char, 4> buffer{};
    co_await socket.readExactly(std::as_writable_bytes(std::span{buffer}));
    response.assign(buffer.data(), buffer.size());
    socket.close();

    ++completed;
    if (completed == 2)
    {
        loop.quit();
    }
}

chaoxi::v2::Task<void> expectConnectionRefused(chaoxi::net::EventLoop& loop,
                                               chaoxi::net::InetAddress address,
                                               bool& refused)
{
    try
    {
        auto socket = co_await chaoxi::v2::AsyncSocket::connect(loop, address);
        socket.close();
    }
    catch (const std::system_error& error)
    {
        refused = error.code().value() == ECONNREFUSED;
    }
    loop.quit();
}

TEST(V2AsyncSocketTest, ConnectAcceptAndExchangeData)
{
    chaoxi::net::EventLoop loop;
    chaoxi::v2::AsyncAcceptor acceptor{
        loop, chaoxi::net::InetAddress{0, true}
    };
    const auto listenAddress = acceptor.localAddress();
    std::string response;
    int completed = 0;
    bool timedOut = false;

    loop.runAfter(1.0,
                  [&]
                  {
                      timedOut = true;
                      loop.quit();
                  });
    chaoxi::v2::spawn(loop, runServer(loop, acceptor, completed));
    chaoxi::v2::spawn(loop, runClient(loop, listenAddress, response, completed));
    loop.loop();

    EXPECT_FALSE(timedOut);
    EXPECT_EQ(completed, 2);
    EXPECT_EQ(response, "pong");
}

TEST(V2AsyncSocketTest, AcceptorUsesEphemeralLoopbackPort)
{
    chaoxi::net::EventLoop loop;
    chaoxi::v2::AsyncAcceptor acceptor{
        loop, chaoxi::net::InetAddress{0, true}
    };

    const auto address = acceptor.localAddress();

    EXPECT_TRUE(acceptor.isOpen());
    EXPECT_EQ(address.toIp(), "127.0.0.1");
    EXPECT_NE(address.port(), 0);
}

TEST(V2AsyncSocketTest, ConnectPropagatesSocketError)
{
    chaoxi::net::EventLoop loop;
    chaoxi::v2::AsyncAcceptor temporary{
        loop, chaoxi::net::InetAddress{0, true}
    };
    const auto unusedAddress = temporary.localAddress();
    temporary.close();
    bool refused = false;

    chaoxi::v2::spawn(loop,
                      expectConnectionRefused(loop, unusedAddress, refused));
    loop.loop();

    EXPECT_TRUE(refused);
}

}  // namespace
