#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Endian.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/Socket.hpp"
#include "chaoxi/net/TcpClient.hpp"

#include <cstddef>
#include <print>

#include <unistd.h>

using namespace chaoxi;

class TimeClient
{
public:
    TimeClient(net::EventLoop* loop, const net::InetAddress& serverAddr)
        : loop_(loop)
        , client_(loop, serverAddr, "TimeClient")
    {
        client_.setConnectionCallback([this](const net::TcpConnectionPtr& conn)
                                      { this->onConnection(conn); });

        client_.setMessageCallback([this](const net::TcpConnectionPtr& conn,
                                          net::Buffer& buf, Timestamp time)
                                   { this->onMessage(conn, buf, time); });
    }

    void connect() { client_.connect(); }

private:
    void onConnection(const net::TcpConnectionPtr& conn)
    {
        LOG_INFO << conn->localAddress().toIpPort() << " -> "
                 << conn->peerAddress().toIpPort() << " is "
                 << (conn->connected() ? "UP" : "DOWN");

        if (!conn->connected())
        {
            loop_->quit();
        }
    }

    void onMessage(const net::TcpConnectionPtr& conn,
                   net::Buffer& buf,
                   Timestamp receiveTime)
    {
        if (buf.readableBytes() >= sizeof(std::size_t))
        {
            const void* data = buf.peek();
            auto be32 = *static_cast<const std::size_t*>(data);
            buf.retrieve(sizeof(std::size_t));

            auto time = net::sockets::networkToHost(be32);
            auto duration = Timestamp::duration(time);
            auto timepoint = Timestamp::clock::time_point(duration);
            // net::time_t time = net::sockets::networkToHost32(be32);
            // net::Timestamp ts(implicit_cast<uint64_t>(time) *
            //              net::Timestamp::kMicroSecondsPerSecond);
            LOG_INFO << "Server time = " << time << ", "
                     << std::format("{:%Y-%m-%d %H:%M:%S}", timepoint);
        }
        else
        {
            LOG_INFO << conn->name() << " no enough data " << buf.readableBytes()
                     << " at "
                     << std::format("{:%Y-%m-%d %H:%M:%S}", receiveTime);
        }
    }

    net::EventLoop* loop_;
    net::TcpClient client_;
};

int main(int argc, char* argv[])
{
    LOG_INFO << "pid = " << getpid();
    if (argc > 1)
    {
        net::EventLoop loop;
        net::InetAddress serverAddr(argv[1], 8888);

        TimeClient timeClient(&loop, serverAddr);
        timeClient.connect();
        loop.loop();
    }
    else
    {
        std::println("Usage: {} host_ip", argv[0]);
    }
}
