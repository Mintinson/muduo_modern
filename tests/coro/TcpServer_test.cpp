#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/coro/AsyncSocket.hpp"
#include "chaoxi/coro/Spawn.hpp"
#include "chaoxi/coro/TcpServer.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace
{

std::span<const std::byte> bytes(std::string_view value)
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

chaoxi::coro::Task<void> echoOnce(chaoxi::coro::AsyncSocket socket, bool& handled)
{
    std::array<char, 5> request{};
    co_await socket.readExactly(std::as_writable_bytes(std::span{request}));
    co_await socket.writeAll(
        std::as_bytes(std::span{request.data(), request.size()}));
    handled = true;
}

chaoxi::coro::Task<void> runUntilStopped(chaoxi::net::EventLoop& loop,
                                       chaoxi::coro::TcpServer& server,
                                       bool& stopped)
{
    co_await server.run();
    stopped = true;
    loop.quit();
}

chaoxi::coro::Task<void> runClient(chaoxi::net::EventLoop& loop,
                                 chaoxi::net::InetAddress address,
                                 chaoxi::coro::TcpServer& server,
                                 std::string& response)
{
    auto socket = co_await chaoxi::coro::AsyncSocket::connect(loop, address);
    co_await socket.writeAll(bytes("hello"));

    std::array<char, 5> buffer{};
    co_await socket.readExactly(std::as_writable_bytes(std::span{buffer}));
    response.assign(buffer.data(), buffer.size());
    server.stop();
}

chaoxi::coro::Task<void> failConnection(chaoxi::coro::AsyncSocket)
{
    throw std::runtime_error("connection handler failure");
    co_return;
}

chaoxi::coro::Task<void> connectOnly(chaoxi::net::EventLoop& loop,
                                   chaoxi::net::InetAddress address)
{
    auto socket = co_await chaoxi::coro::AsyncSocket::connect(loop, address);
}

TEST(CoroTcpServerTest, RunsCoroutineConnectionHandler)
{
    chaoxi::net::EventLoop loop;
    bool handled = false;
    bool stopped = false;
    bool timedOut = false;
    std::string response;

    chaoxi::coro::TcpServer server{
        loop, chaoxi::net::InetAddress{0, true},
        [&handled](chaoxi::coro::AsyncSocket socket,
                   const chaoxi::net::InetAddress&)
        { return echoOnce(std::move(socket), handled);    }
    };

    loop.runAfter(1.0,
                  [&]
                  {
                      timedOut = true;
                      server.stop();
                  });
    chaoxi::coro::spawn(loop, runUntilStopped(loop, server, stopped));
    chaoxi::coro::spawn(loop,
                      runClient(loop, server.localAddress(), server, response));
    loop.loop();

    EXPECT_FALSE(timedOut);
    EXPECT_TRUE(handled);
    EXPECT_TRUE(stopped);
    EXPECT_FALSE(server.running());
    EXPECT_EQ(response, "hello");
}

TEST(CoroTcpServerTest, ForwardsConnectionHandlerErrors)
{
    chaoxi::net::EventLoop loop;
    bool errorHandled = false;
    bool stopped = false;
    chaoxi::coro::TcpServer server{
        loop, chaoxi::net::InetAddress{0, true},
        [](chaoxi::coro::AsyncSocket socket, const chaoxi::net::InetAddress&)
        { return failConnection(std::move(socket)); }
    };
    server.setErrorHandler(
        [&](std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::runtime_error&)
            {
                errorHandled = true;
            }
            server.stop();
        });

    chaoxi::coro::spawn(loop, runUntilStopped(loop, server, stopped));
    chaoxi::coro::spawn(loop, connectOnly(loop, server.localAddress()));
    loop.loop();

    EXPECT_TRUE(errorHandled);
    EXPECT_TRUE(stopped);
}

}  // namespace
