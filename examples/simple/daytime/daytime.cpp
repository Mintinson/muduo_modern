#include "daytime.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <string>

DayTimeServer::DayTimeServer(chaoxi::net::EventLoop* loop,
                             const chaoxi::net::InetAddress& listenAddr)
    : loop_(loop)
    , server_(loop, listenAddr, "DayTimeServer")
{
    server_.setConnectionCallback(
        [this](const chaoxi::net::TcpConnectionPtr& conn)
        { this->onConnection(conn); });

    server_.setMessageCallback(
        [this](const chaoxi::net::TcpConnectionPtr& conn,
               chaoxi::net::Buffer& buf, chaoxi::Timestamp time)
        { this->onMessage(conn, buf, time); });
}

void DayTimeServer::start()
{
    server_.start();
}

void DayTimeServer::onConnection(const chaoxi::net::TcpConnectionPtr& conn)
{
    FLOG_INFO("DayTimeServer - {} -> {} is {}", conn->peerAddress().toIpPort(),
              conn->localAddress().toIpPort(),
              (conn->connected() ? "UP" : "DOWN"));
    if (conn->connected())
    {
        conn->send(std::format("{:%Y-%m-%d %H:%M:%S}\n", chaoxi::Timestamp::clock::now()));
        conn->shutdown();  // 发送完毕后主动断开连接
    }
}

void DayTimeServer::onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                              chaoxi::net::Buffer& buf,
                              chaoxi::Timestamp time) {
    std::string msg(buf.retrieveAllAsString());
    LOG_INFO << conn->name() << " received " << msg.size()
             << " bytes received at "
             << std::format("{:%Y-%m-%d %H:%M:%S}", time);
    // Do nothing with the message, just discard it
}