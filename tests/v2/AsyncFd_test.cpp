#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/v2/AsyncFd.hpp"
#include "chaoxi/v2/Spawn.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <span>
#include <string>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

void makeNonblocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ASSERT_EQ(::fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
}

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
                                       int peerFd,
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
    if (::write(peerFd, &byte, sizeof(byte)) !=
        static_cast<ssize_t>(sizeof(byte)))
    {
        throw std::system_error(errno, std::system_category());
    }
}

TEST(V2AsyncFdTest, ReadsAfterReadinessNotification)
{
    int sockets[2]{};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
    makeNonblocking(sockets[0]);

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
                          ::write(sockets[1], message.data(), message.size()),
                          static_cast<ssize_t>(message.size()));
                  });

    chaoxi::v2::spawn(
        loop, readMessage(loop, chaoxi::v2::AsyncFd{loop, sockets[0]}, result));
    loop.loop();
    ::close(sockets[1]);

    EXPECT_FALSE(timedOut);
    EXPECT_EQ(result, "hello");
}

TEST(V2AsyncFdTest, CloseCancelsPendingRead)
{
    int sockets[2]{};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
    makeNonblocking(sockets[0]);

    chaoxi::net::EventLoop loop;
    chaoxi::v2::AsyncFd fd{loop, sockets[0]};
    bool cancelled = false;
    loop.runAfter(0.001, [&fd] { fd.close(); });

    chaoxi::v2::spawn(loop, waitForCancellation(loop, fd, cancelled));
    loop.loop();
    ::close(sockets[1]);

    EXPECT_TRUE(cancelled);
}

TEST(V2AsyncFdTest, ReportsPeerEof)
{
    int sockets[2]{};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
    makeNonblocking(sockets[0]);
    ::close(sockets[1]);

    chaoxi::net::EventLoop loop;
    std::size_t bytes = 1;
    chaoxi::v2::spawn(
        loop, readEof(loop, chaoxi::v2::AsyncFd{loop, sockets[0]}, bytes));
    loop.loop();

    EXPECT_EQ(bytes, 0U);
}

TEST(V2AsyncFdTest, WriteAllHandlesBackpressure)
{
    int sockets[2]{};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
    makeNonblocking(sockets[0]);
    const int sendBufferSize = 4'096;
    ASSERT_EQ(::setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &sendBufferSize,
                           static_cast<socklen_t>(sizeof(sendBufferSize))),
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
                const ssize_t count =
                    ::read(sockets[1], buffer.data(), buffer.size());
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
        writePayload(loop, chaoxi::v2::AsyncFd{loop, sockets[0]},
                     std::as_bytes(std::span{payload.data(), payload.size()}),
                     completed));
    loop.loop();
    reader.join();
    ::close(sockets[1]);

    EXPECT_TRUE(completed);
    EXPECT_EQ(received, payload);
}

TEST(V2AsyncFdTest, RejectsConcurrentReadsOnSameFd)
{
    int sockets[2]{};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), 0);
    makeNonblocking(sockets[0]);

    chaoxi::net::EventLoop loop;
    chaoxi::v2::AsyncFd fd{loop, sockets[0]};
    bool firstCompleted = false;
    bool duplicateRejected = false;

    chaoxi::v2::spawn(loop, firstReader(loop, fd, firstCompleted));
    chaoxi::v2::spawn(loop, duplicateReader(fd, sockets[1], duplicateRejected));
    loop.loop();
    ::close(sockets[1]);

    EXPECT_TRUE(firstCompleted);
    EXPECT_TRUE(duplicateRejected);
}

}  // namespace
