
#include "time.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Endian.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <cstddef>
#include <string>

TimeServer::TimeServer(chaoxi::net::EventLoop* loop,
                       const chaoxi::net::InetAddress& listenAddr)
    : loop_(loop)
    , server_(loop, listenAddr, "TimeServer")
{
    server_.setConnectionCallback(
        [this](const chaoxi::net::TcpConnectionPtr& conn)
        { this->onConnection(conn); });
}

void TimeServer::start()
{
    server_.start();
}

void TimeServer::onConnection(const chaoxi::net::TcpConnectionPtr& conn)
{
    FLOG_INFO("TimeServer - {} -> {} is {}", conn->peerAddress().toIpPort(),
              conn->localAddress().toIpPort(),
              (conn->connected() ? "UP" : "DOWN"));
    if (conn->connected())
    {
        auto count = static_cast<std::size_t>(
            chaoxi::Timestamp::clock::now().time_since_epoch().count());
        auto ret = chaoxi::net::sockets::hostToNetwork(count);
        conn->send({reinterpret_cast<const char*>(&ret), sizeof(ret)});
        // conn->send(std::format(
        //     "{}\n",
        //     chaoxi::Timestamp::clock::now().time_since_epoch().count()));
        conn->shutdown();  // 发送完毕后主动断开连接
    }
}
