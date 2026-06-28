#include "echo.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <string>

EchoServer::EchoServer(chaoxi::net::EventLoop* loop,
                       const chaoxi::net::InetAddress& listenAddr)
    : loop_(loop)
    , server_(loop, listenAddr, "EchoServer")
{
    server_.setConnectionCallback(
        [this](const chaoxi::net::TcpConnectionPtr& conn)
        { this->onConnection(conn); });

    server_.setMessageCallback(
        [this](const chaoxi::net::TcpConnectionPtr& conn,
               chaoxi::net::Buffer& buf, chaoxi::Timestamp time)
        { this->onMessage(conn, buf, time); });
}

void EchoServer::start()
{
    server_.start();
}

void EchoServer::onConnection(const chaoxi::net::TcpConnectionPtr& conn)
{
    FLOG_INFO("EchoServer - {} -> {} is {}", conn->peerAddress().toIpPort(),
              conn->localAddress().toIpPort(),
              (conn->connected() ? "UP" : "DOWN"));
}

void EchoServer::onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                           chaoxi::net::Buffer& buf,
                           chaoxi::Timestamp time)
{
    std::string msg{buf.retrieveAllAsString()};

    FLOG_INFO("{} echo {} bytes, data received at {} from {}", conn->name(),
              msg.size(), time, conn->peerAddress().toIpPort());
    conn->send(msg);
}