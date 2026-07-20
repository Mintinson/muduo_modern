#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/Socket.hpp"
#include "chaoxi/net/SocketOps.hpp"
#include "chaoxi/net/TcpClient.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <chrono>
#include <ctime>
#include <print>

using namespace chaoxi;
using namespace chaoxi::net;

constexpr size_t frameLen = 2 * sizeof(int64_t);

int createNonblockingUDP()
{
    int sockfd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          IPPROTO_UDP);
    if (sockfd < 0)
    {
        LOG_SYSFATAL << "::socket";
    }
    return sockfd;
}

/////////////////////////////// Server ///////////////////////////////

void serverReadCallback(int sockfd, chaoxi::Timestamp receiveTime)
{
    int64_t message[2];
    struct sockaddr peerAddr{};
    //   memZero(&peerAddr, sizeof peerAddr);
    socklen_t addrLen = sizeof peerAddr;
    ssize_t nr =
        ::recvfrom(sockfd, message, sizeof message, 0, &peerAddr, &addrLen);

    char addrStr[64];
    sockets::toIpPort(addrStr, sizeof addrStr, &peerAddr);
    LOG_DEBUG << "received " << nr << " bytes from " << addrStr;

    if (nr < 0)
    {
        LOG_SYSERR << "::recvfrom";
    }
    else if (static_cast<size_t>(nr) == frameLen)
    {
        message[1] = std::chrono::duration_cast<std::chrono::microseconds>(
                         receiveTime.time_since_epoch())
                         .count();
        ssize_t nw =
            ::sendto(sockfd, message, sizeof message, 0, &peerAddr, addrLen);
        if (nw < 0)
        {
            LOG_SYSERR << "::sendto";
        }
        else if (static_cast<size_t>(nw) != frameLen)
        {
            LOG_ERROR << "Expect " << frameLen << " bytes, wrote " << nw
                      << " bytes.";
        }
    }
    else
    {
        LOG_ERROR << "Expect " << frameLen << " bytes, received " << nr
                  << " bytes.";
    }
}

void runServer(uint16_t port)
{
    Socket sock(createNonblockingUDP());
    sock.bindAddress(InetAddress(port));
    EventLoop loop;
    Channel channel(&loop, sock.fd());
    channel.setReadCallback(
        [capture0 = sock.fd()](auto&& PH1)
        { serverReadCallback(capture0, std::forward<decltype(PH1)>(PH1)); });
    channel.enableReading();
    loop.loop();
}

/////////////////////////////// Client ///////////////////////////////

void clientReadCallback(int sockfd, chaoxi::Timestamp receiveTime)
{
    int64_t message[2];
    ssize_t nr = sockets::read(sockfd, message, sizeof message);

    if (nr < 0)
    {
        LOG_SYSERR << "::read";
    }
    else if (static_cast<size_t>(nr) == frameLen)
    {
        int64_t send = message[0];
        int64_t their = message[1];
        int64_t back = std::chrono::duration_cast<std::chrono::microseconds>(
                           receiveTime.time_since_epoch())
                           .count();
        int64_t mine = (back + send) / 2;
        LOG_INFO << "round trip " << back - send << " clock error "
                 << their - mine;
    }
    else
    {
        LOG_ERROR << "Expect " << frameLen << " bytes, received " << nr
                  << " bytes.";
    }
}

void sendMyTime(int sockfd)
{
    int64_t message[2] = {0, 0};
    message[0] = std::chrono::duration_cast<std::chrono::microseconds>(
                     Timestamp::clock::now().time_since_epoch())
                     .count();
    ssize_t nw = sockets::write(sockfd, message, sizeof message);
    if (nw < 0)
    {
        LOG_SYSERR << "::write";
    }
    else if (static_cast<size_t>(nw) != frameLen)
    {
        LOG_ERROR << "Expect " << frameLen << " bytes, wrote " << nw
                  << " bytes.";
    }
}

void runClient(const char* ip, uint16_t port)
{
    Socket sock(createNonblockingUDP());
    InetAddress serverAddr(ip, port);
    int ret = sockets::connect(sock.fd(), serverAddr.getSockAddr());
    if (ret < 0)
    {
        LOG_SYSFATAL << "::connect";
    }
    EventLoop loop;
    Channel channel(&loop, sock.fd());
    channel.setReadCallback(
        [capture0 = sock.fd()](auto&& PH1)
        { clientReadCallback(capture0, std::forward<decltype(PH1)>(PH1)); });
    channel.enableReading();
    loop.runEvery(0.2, std::bind(sendMyTime, sock.fd()));
    loop.loop();
}

int main(int argc, char* argv[])
{
    if (argc > 2)
    {
        auto port = static_cast<uint16_t>(atoi(argv[2]));
        if (strcmp(argv[1], "-s") == 0)
        {
            runServer(port);
        }
        else
        {
            runClient(argv[1], port);
        }
    }
    else
    {
        std::println("Usage:\n{} -s port\n{} ip port", argv[0], argv[0]);
    }
}
