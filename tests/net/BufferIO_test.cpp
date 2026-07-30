#include "chaoxi/net/Buffer.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

class SocketPair
{
public:
    SocketPair() { EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0); }

    ~SocketPair()
    {
        if (fds_[0] >= 0)
        {
            ::close(fds_[0]);
        }
        if (fds_[1] >= 0)
        {
            ::close(fds_[1]);
        }
    }

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    [[nodiscard]] int reader() const noexcept { return fds_[0]; }

    [[nodiscard]] int writer() const noexcept { return fds_[1]; }

private:
    int fds_[2]{-1, -1};
};

TEST(BufferIOTest, ReadFdUsesWritableAreaAndExtraBuffer)
{
    SocketPair sockets;
    chaoxi::net::Buffer buffer(16);
    buffer.append("head");
    const std::string payload(8'192, 'x');
    ASSERT_EQ(::write(sockets.writer(), payload.data(), payload.size()),
              static_cast<ssize_t>(payload.size()));

    int savedErrno = 0;
    const ssize_t bytes = buffer.readFd(sockets.reader(), &savedErrno);

    EXPECT_EQ(bytes, static_cast<ssize_t>(payload.size()));
    EXPECT_EQ(savedErrno, 0);
    EXPECT_EQ(buffer.readableBytes(), payload.size() + 4);
    EXPECT_EQ(buffer.retrieveAllAsString(), "head" + payload);
}

TEST(BufferIOTest, ReadFdPreservesSystemError)
{
    chaoxi::net::Buffer buffer;
    int savedErrno = 0;

    EXPECT_EQ(buffer.readFd(-1, &savedErrno), -1);
    EXPECT_EQ(savedErrno, EBADF);
    EXPECT_EQ(buffer.readableBytes(), 0U);
}

TEST(BufferIOTest, WritableSpanAndUnwriteSupportZeroCopyProducers)
{
    chaoxi::net::Buffer buffer(8);
    const std::string payload = "abcdefgh";

    auto writable = buffer.writableSpan();
    ASSERT_GE(writable.size(), payload.size());
    std::memcpy(writable.data(), payload.data(), payload.size());
    buffer.hasWritten(payload.size());
    buffer.unwrite(3);

    EXPECT_EQ(buffer.toStringPiece(), "abcde");
}

}  // namespace
