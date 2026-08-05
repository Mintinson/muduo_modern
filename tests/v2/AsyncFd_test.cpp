#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/SocketOps.hpp"
#include "chaoxi/v2/AsyncFd.hpp"
#include "chaoxi/v2/Spawn.hpp"
#include "ConnectedSockets.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <span>
#include <string>
#include <system_error>
#include <thread>

#include <gtest/gtest.h>

namespace
{

chaoxi::v2::Task<void> readMessage(chaoxi::net::EventLoop& loop,
                                   chaoxi::v2::AsyncFd fd,
                                   std::string& result)
{
    std::array<char, 5> buffer{};
    co_await fd.readExactly(std::as_writable_bytes(std::span{buffer}));
    result.assign(buffer.data(), buffer.size());
    loop.quit();
}

chaoxi::v2::Task<void> waitForCancellation(chaoxi::net::EventLoop& loop,
                                           chaoxi::v2::AsyncFd& fd,
                                           bool& cancelled)
{
    std::array<std::byte, 8> buffer{};
    try
    {
        (void)co_await fd.readSome(buffer);
    }
    catch (const std::system_error& error)
    {
        cancelled =
            error.code() == std::make_error_code(std::errc::operation_canceled);
    }
    loop.quit();
}

chaoxi::v2::Task<void> readEof(chaoxi::net::EventLoop& loop,
                               chaoxi::v2::AsyncFd fd,
                               std::size_t& bytes)
{
    std::array<std::byte, 8> buffer{};
    bytes = co_await fd.readSome(buffer);
    loop.quit();
}

chaoxi::v2::Task<void> writePayload(chaoxi::net::EventLoop& loop,
                                    chaoxi::v2::AsyncFd fd,
                                    std::span<const std::byte> payload,
                                    bool& completed)
{
    co_await fd.writeAll(payload);
    completed = true;
    fd.close();
    loop.quit();
}

chaoxi::v2::Task<void> firstReader(chaoxi::net::EventLoop& loop,
                                   chaoxi::v2::AsyncFd& fd,
                                   bool& completed)
{
    std::array<std::byte, 1> buffer{};
    (void)co_await fd.readSome(buffer);
    completed = true;
    loop.quit();
}

chaoxi::v2::Task<void> duplicateReader(chaoxi::v2::AsyncFd& fd,
                                       chaoxi::net::SocketHandle peerFd,
                                       bool& rejected)
{
    std::array<std::byte, 1> buffer{};
    try
    {
        (void)co_await fd.readSome(buffer);
    }
    catch (const std::system_error& error)
    {
        rejected = error.code() ==
                   std::make_error_code(std::errc::operation_in_progress);
    }

    const char byte = 'x';
    if (chaoxi::net::sockets::write(peerFd, &byte, sizeof(byte)) !=
        static_cast<chaoxi::net::SignedSize>(sizeof(byte)))
    {
        throw std::system_error(errno, std::generic_category());
    }
}

TEST(V2AsyncFdTest, ReadsAfterReadinessNotification)
{
    chaoxi::v2::test::ConnectedSockets sockets;

    chaoxi::net::EventLoop loop;
    std::string result;
    bool timedOut = false;
    loop.runAfter(1.0,
                  [&]
                  {
                      timedOut = true;
                      loop.quit();
                  });
    loop.runAfter(0.001,
                  [&]
                  {
                      constexpr std::string_view message = "hello";
                      EXPECT_EQ(
                          chaoxi::net::sockets::write(
                              sockets.second(), message.data(), message.size()),
                          static_cast<chaoxi::net::SignedSize>(message.size()));
                  });

    chaoxi::v2::spawn(
        loop,
        readMessage(loop,
                    chaoxi::v2::AsyncFd{loop, sockets.releaseFirst()}, result));
    loop.loop();

    EXPECT_FALSE(timedOut);
    EXPECT_EQ(result, "hello");
}

TEST(V2AsyncFdTest, CloseCancelsPendingRead)
{
    chaoxi::v2::test::ConnectedSockets sockets;

    chaoxi::net::EventLoop loop;
    chaoxi::v2::AsyncFd fd{loop, sockets.releaseFirst()};
    bool cancelled = false;
    loop.runAfter(0.001, [&fd] { fd.close(); });

    chaoxi::v2::spawn(loop, waitForCancellation(loop, fd, cancelled));
    loop.loop();

    EXPECT_TRUE(cancelled);
}

TEST(V2AsyncFdTest, ReportsPeerEof)
{
    chaoxi::v2::test::ConnectedSockets sockets;
    sockets.closeSecond();

    chaoxi::net::EventLoop loop;
    std::size_t bytes = 1;
    chaoxi::v2::spawn(
        loop,
        readEof(loop, chaoxi::v2::AsyncFd{loop, sockets.releaseFirst()}, bytes));
    loop.loop();

    EXPECT_EQ(bytes, 0U);
}

TEST(V2AsyncFdTest, WriteAllHandlesBackpressure)
{
    chaoxi::v2::test::ConnectedSockets sockets;
    const int sendBufferSize = 4'096;
    ASSERT_EQ(chaoxi::net::sockets::setSocketOption(
                  sockets.first(), SOL_SOCKET, SO_SNDBUF, sendBufferSize),
              0);

    const std::string payload(512 * 1'024, 'z');
    std::string received;
    received.reserve(payload.size());
    std::jthread reader(
        [&]
        {
            std::array<char, 8'192> buffer{};
            while (received.size() < payload.size())
            {
                const chaoxi::net::SignedSize count =
                    chaoxi::net::sockets::read(
                        sockets.second(), buffer.data(), buffer.size());
                if (count > 0)
                {
                    received.append(buffer.data(),
                                    static_cast<std::size_t>(count));
                }
                else if (count < 0 && errno == EINTR)
                {
                    continue;
                }
                else
                {
                    break;
                }
            }
        });

    chaoxi::net::EventLoop loop;
    bool completed = false;
    chaoxi::v2::spawn(
        loop,
        writePayload(loop, chaoxi::v2::AsyncFd{loop, sockets.releaseFirst()},
                     std::as_bytes(std::span{payload.data(), payload.size()}),
                     completed));
    loop.loop();
    reader.join();

    EXPECT_TRUE(completed);
    EXPECT_EQ(received, payload);
}

TEST(V2AsyncFdTest, RejectsConcurrentReadsOnSameFd)
{
    chaoxi::v2::test::ConnectedSockets sockets;

    chaoxi::net::EventLoop loop;
    chaoxi::v2::AsyncFd fd{loop, sockets.releaseFirst()};
    bool firstCompleted = false;
    bool duplicateRejected = false;

    chaoxi::v2::spawn(loop, firstReader(loop, fd, firstCompleted));
    chaoxi::v2::spawn(
        loop, duplicateReader(fd, sockets.second(), duplicateRejected));
    loop.loop();

    EXPECT_TRUE(firstCompleted);
    EXPECT_TRUE(duplicateRejected);
}

}  // namespace
