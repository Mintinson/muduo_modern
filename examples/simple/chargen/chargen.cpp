#include "chargen.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <chrono>
#include <print>
#include <string>

ChargenServer::ChargenServer(chaoxi::net::EventLoop* loop,
                             const chaoxi::net::InetAddress& listenAddr,
                             bool print)
    : loop_(loop)
    , server_(loop, listenAddr, "ChargenServer")
{
    server_.setConnectionCallback(
        [this](const chaoxi::net::TcpConnectionPtr& conn)
        { this->onConnection(conn); });

    server_.setMessageCallback(
        [this](const chaoxi::net::TcpConnectionPtr& conn,
               chaoxi::net::Buffer& buf, chaoxi::Timestamp time)
        { this->onMessage(conn, buf, time); });
    server_.setWriteCompleteCallback(
        [this](const chaoxi::net::TcpConnectionPtr& conn)
        { this->onWriteComplete(conn); });

    if (print)
    {
        loop->runEvery(3.0, [this] { printThroughput(); });
    }

    std::string line;
    for (int i = 33; i < 127; ++i)
    {
        line.push_back(char(i));
    }
    line += line;

    for (size_t i = 0; i < 127 - 33; ++i)
    {
        message_ += line.substr(i, 72) + '\n';
    }
}

void ChargenServer::start()
{
    server_.start();
}

void ChargenServer::onConnection(const chaoxi::net::TcpConnectionPtr& conn)
{
    FLOG_INFO("ChargenServer - {} -> {} is {}", conn->peerAddress().toIpPort(),
              conn->localAddress().toIpPort(),
              (conn->connected() ? "UP" : "DOWN"));
    if (conn->connected())
    {
        // transferred_ = 0;
        conn->setTcpNoDelay(true);
        conn->send(message_);
    }
}

void ChargenServer::onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                              chaoxi::net::Buffer& buf,
                              chaoxi::Timestamp time)
{
    std::string msg{buf.retrieveAllAsString()};

    FLOG_INFO("{} echo {} bytes, data received at {} from {}", conn->name(),
              msg.size(), time, conn->peerAddress().toIpPort());
}

void ChargenServer::onWriteComplete(const chaoxi::net::TcpConnectionPtr& conn)
{
    transferred_ += message_.size();
    conn->send(message_);
}

void ChargenServer::printThroughput()
{
    auto endTime = chaoxi::Timestamp::clock::now();
    auto time =
        std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime_)
            .count();
    std::println("{:4.3f} MiB/s",
           static_cast<double>(transferred_) / time / 1024 / 1024);
    transferred_ = 0;
    startTime_ = endTime;
}
