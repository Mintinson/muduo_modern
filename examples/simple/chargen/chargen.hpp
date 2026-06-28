#pragma once

#include <chaoxi/net/TcpServer.hpp>

class ChargenServer
{
public:
    ChargenServer(chaoxi::net::EventLoop* loop,
                  const chaoxi::net::InetAddress& listenAddr,
                  bool print = false);
    void start();  // calls server_.start();

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn);

    void onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf,
                   chaoxi::Timestamp time);
    void onWriteComplete(const chaoxi::net::TcpConnectionPtr& conn);
    void printThroughput();
    chaoxi::net::EventLoop* loop_;
    chaoxi::net::TcpServer server_;
    chaoxi::Timestamp startTime_;
    std::string message_;
    int64_t transferred_;
};