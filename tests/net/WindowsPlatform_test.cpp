#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThread.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/SocketOps.hpp"
#include "chaoxi/net/TcpConnection.hpp"
#include "chaoxi/net/TcpServer.hpp"
#include "chaoxi/net/Buffer.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <gtest/gtest.h>

using namespace std::chrono_literals;
using namespace chaoxi::net;

TEST(WindowsPlatform, SocketLifecycleAndAddressConversion)
{
    const SocketHandle socket = sockets::createNonblockingOrDie(AF_INET);
    EXPECT_NE(socket, kInvalidSocket);

    sockaddr_in address{};
    sockets::fromIpPort("127.0.0.1", 8080, &address);
    EXPECT_EQ(sockets::toIpPort(reinterpret_cast<const sockaddr*>(&address)),
              "127.0.0.1:8080");
    sockets::close(socket);
}

TEST(WindowsPlatform, TimerDrivesEventLoopWithoutTimerFd)
{
    EventLoop loop;
    std::atomic<bool> fired{false};
    const auto start = std::chrono::steady_clock::now();
    loop.runAfter(0.05, [&] {
        fired.store(true, std::memory_order_release);
        loop.quit();
    });
    loop.loop();

    EXPECT_TRUE(fired.load(std::memory_order_acquire));
    EXPECT_GE(std::chrono::steady_clock::now() - start, 30ms);
}

TEST(WindowsPlatform, LoopbackSocketWakesAnotherThread)
{
    EventLoopThread thread;
    EventLoop* loop = thread.startLoop();
    std::promise<void> completed;
    auto future = completed.get_future();

    loop->queueInLoop([&] { completed.set_value(); });
    EXPECT_EQ(future.wait_for(2s), std::future_status::ready);
    loop->quit();
}

TEST(WindowsPlatform, TcpServerAcceptsAndReadsWithWindowsReactor)
{
    ensureNetworkInitialized();
    SocketHandle probe = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(probe, kInvalidSocket);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    ASSERT_NE(::bind(probe, reinterpret_cast<const sockaddr*>(&address), sizeof address),
              SOCKET_ERROR);
    int addressLength = sizeof address;
    ASSERT_NE(::getsockname(probe, reinterpret_cast<sockaddr*>(&address), &addressLength),
              SOCKET_ERROR);
    const std::uint16_t port = ntohs(address.sin_port);
    ::closesocket(probe);

    EventLoop loop;
    TcpServer server(&loop, InetAddress(port, true), "windows_tcp_test");
    std::atomic<bool> received{false};
    server.setMessageCallback([&](const TcpConnectionPtr&, Buffer& buffer, chaoxi::Timestamp) {
        received.store(buffer.retrieveAllAsString() == "ping", std::memory_order_release);
    });
    server.setConnectionCallback([&](const TcpConnectionPtr& connection) {
        if (!connection->connected() && received.load(std::memory_order_acquire))
        {
            loop.quit();
        }
    });
    server.start();

    std::jthread client([port] {
        SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        destination.sin_port = htons(port);
        if (::connect(socket, reinterpret_cast<const sockaddr*>(&destination),
                      sizeof destination) == 0)
        {
            (void)::send(socket, "ping", 4, 0);
            (void)::shutdown(socket, SD_SEND);
        }
        ::closesocket(socket);
    });

    loop.runAfter(2.0, [&] { loop.quit(); });
    loop.loop();
    EXPECT_TRUE(received.load(std::memory_order_acquire));
}
