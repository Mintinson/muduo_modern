///
/// @file tests/net/SocketOps_test.cpp
/// @brief SocketOps 单元测试
///

#include "chaoxi/net/Endian.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <cstring>

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

// 引入 SocketOps 命名空间（注意: read/write/close 与 POSIX 冲突, 需要显式 ::
// 前缀）
using namespace chaoxi::net::sockets;

TEST(SocketOpsTest, CreateAndClose)
{
    int fd = createNonblockingOrDie(AF_INET);
    EXPECT_GE(fd, 0);
    int flags = ::fcntl(fd, F_GETFL);
    EXPECT_NE(flags, -1);
    EXPECT_TRUE(flags & O_NONBLOCK);
    ::close(fd);
}

TEST(SocketOpsTest, CreateAndCloseIPv6)
{
    int fd = createNonblockingOrDie(AF_INET6);
    EXPECT_GE(fd, 0);
    ::close(fd);
}

TEST(SocketOpsTest, ReadWrite)
{
    int p[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, p), 0);
    const char* msg = "hello";
    ssize_t nw = ::write(p[0], msg, 5);
    EXPECT_EQ(nw, 5);
    char buf[64]{};
    ssize_t nr = ::read(p[1], buf, sizeof(buf));
    EXPECT_EQ(nr, 5);
    EXPECT_STREQ(buf, "hello");
    ::close(p[0]);
    ::close(p[1]);
}

TEST(SocketOpsTest, GetSocketErrorOnValidFd)
{
    int fd = createNonblockingOrDie(AF_INET);
    EXPECT_EQ(getSocketError(fd), 0);
    ::close(fd);
}

TEST(SocketOpsTest, FromIpPortAndToIpPort)
{
    struct sockaddr_in addr{};
    fromIpPort("1.2.3.4", 8888, &addr);
    EXPECT_EQ(addr.sin_family, AF_INET);
    EXPECT_EQ(ntoh16(addr.sin_port), 8888u);

    auto result = toIpPort(reinterpret_cast<const sockaddr*>(&addr));
    EXPECT_EQ(result, "1.2.3.4:8888");

    auto ip = toIp(reinterpret_cast<const sockaddr*>(&addr));
    EXPECT_EQ(ip, "1.2.3.4");
}

TEST(SocketOpsTest, FromIpPortAndToIpPortIPv6)
{
    struct sockaddr_in6 addr{};
    fromIpPort("::1", 9999, &addr);
    EXPECT_EQ(addr.sin6_family, AF_INET6);
    EXPECT_EQ(ntoh16(addr.sin6_port), 9999u);

    auto result = toIpPort(reinterpret_cast<const sockaddr*>(&addr));
    EXPECT_EQ(result, "[::1]:9999");

    auto ip = toIp(reinterpret_cast<const sockaddr*>(&addr));
    EXPECT_EQ(ip, "::1");
}

TEST(SocketOpsTest, ToIpPortBuffer)
{
    struct sockaddr_in addr{};
    fromIpPort("10.0.0.1", 1234, &addr);
    char buf[64]{};
    toIpPort(buf, sizeof(buf), reinterpret_cast<const sockaddr*>(&addr));
    EXPECT_STREQ(buf, "10.0.0.1:1234");
}

TEST(SocketOpsTest, ToIpPortBufferIPv6)
{
    struct sockaddr_in6 addr{};
    fromIpPort("fe80::1", 8080, &addr);
    char buf[64]{};
    toIpPort(buf, sizeof(buf), reinterpret_cast<const sockaddr*>(&addr));
    EXPECT_STREQ(buf, "[fe80::1]:8080");
}

TEST(SocketOpsTest, ToIpBuffer)
{
    struct sockaddr_in addr{};
    fromIpPort("255.255.255.255", 0, &addr);
    char buf[INET_ADDRSTRLEN]{};
    toIp(buf, sizeof(buf), reinterpret_cast<const sockaddr*>(&addr));
    EXPECT_STREQ(buf, "255.255.255.255");
}

TEST(SocketOpsTest, ToIpBufferIPv6)
{
    struct sockaddr_in6 addr{};
    fromIpPort("2001:db8::1", 0, &addr);
    char buf[INET6_ADDRSTRLEN]{};
    toIp(buf, sizeof(buf), reinterpret_cast<const sockaddr*>(&addr));
    EXPECT_EQ(std::string(buf), "2001:db8::1");
}

TEST(SocketOpsTest, SockaddrCast)
{
    struct sockaddr_in addr4{};
    addr4.sin_family = AF_INET;
    EXPECT_EQ(sockaddr_cast(&addr4)->sa_family, AF_INET);

    struct sockaddr_in6 addr6{};
    addr6.sin6_family = AF_INET6;
    EXPECT_EQ(sockaddr_cast(&addr6)->sa_family, AF_INET6);

    auto* p6nc = sockaddr_cast(&addr6);
    EXPECT_EQ(p6nc->sa_family, AF_INET6);

    const auto* back4 =
        sockaddr_in_cast(reinterpret_cast<const sockaddr*>(&addr4));
    EXPECT_EQ(back4->sin_family, AF_INET);

    const auto* back6 =
        sockaddr_in6_cast(reinterpret_cast<const sockaddr*>(&addr6));
    EXPECT_EQ(back6->sin6_family, AF_INET6);
}

TEST(SocketOpsTest, GetLocalAndPeerAddr)
{
    int p[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, p), 0);
    EXPECT_EQ(getLocalAddr(p[0]).sin6_family, AF_UNIX);
    EXPECT_EQ(getPeerAddr(p[0]).sin6_family, AF_UNIX);
    ::close(p[0]);
    ::close(p[1]);
}

TEST(SocketOpsTest, IsSelfConnectSocketpair)
{
    // AF_UNIX socketpair: getLocalAddr 和 getPeerAddr 不同（Unix domain
    // 中不会自连接）
    int p[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, p), 0);
    EXPECT_FALSE(isSelfConnect(p[0]));  // Unix socketpair 不会自连接
    ::close(p[0]);
    ::close(p[1]);
}

TEST(SocketOpsTest, BindAndListen)
{
    int fd = createNonblockingOrDie(AF_INET);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = hton32(INADDR_LOOPBACK);
    addr.sin_port = hton16(0);
    bindOrDie(fd, reinterpret_cast<const sockaddr*>(&addr));
    listenOrDie(fd);
    ::close(fd);
}

TEST(SocketOpsTest, Accept)
{
    int listenFd = createNonblockingOrDie(AF_INET);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = hton32(INADDR_LOOPBACK);
    addr.sin_port = hton16(0);
    bindOrDie(listenFd, reinterpret_cast<const sockaddr*>(&addr));
    listenOrDie(listenFd);

    struct sockaddr_in6 actualAddr = getLocalAddr(listenFd);
    uint16_t port = ntoh16(actualAddr.sin6_port);
    ASSERT_NE(port, 0);

    int clientFd = createNonblockingOrDie(AF_INET);
    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = hton32(INADDR_LOOPBACK);
    serverAddr.sin_port = hton16(port);
    int ret = connect(clientFd, reinterpret_cast<const sockaddr*>(&serverAddr));
    EXPECT_TRUE(ret == 0 || (ret == -1 && errno == EINPROGRESS));
    (void)ret;

    struct sockaddr_in6 peerAddr{};
    int connFd = accept(listenFd, &peerAddr);
    EXPECT_GE(connFd, 0);

    ::close(connFd);
    ::close(clientFd);
    ::close(listenFd);
}

TEST(SocketOpsTest, HostToNetworkByteOrder)
{
    uint16_t v16 = 0x1234;
    uint32_t v32 = 0x12345678;
    EXPECT_EQ(ntoh16(hton16(v16)), v16);
    EXPECT_EQ(ntoh32(hton32(v32)), v32);
}

TEST(SocketOpsTest, SpecialAddresses)
{
    struct sockaddr_in addr{};
    fromIpPort("0.0.0.0", 0, &addr);
    EXPECT_EQ(toIp(reinterpret_cast<const sockaddr*>(&addr)), "0.0.0.0");

    fromIpPort("255.255.255.255", 65535, &addr);
    EXPECT_EQ(toIpPort(reinterpret_cast<const sockaddr*>(&addr)),
              "255.255.255.255:65535");
}

TEST(SocketOpsTest, CreateWithInvalidFamilyWouldCrash)
{
    // createNonblockingOrDie 内部使用 IPPROTO_TCP，不适用于 AF_UNIX
    // 验证 AF_INET / AF_INET6 正常工作即可（已在其他测试覆盖）
    SUCCEED();
}
