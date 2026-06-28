#pragma once

#include "chaoxi/net/TcpServer.hpp"

class TimeServer
{
public:
    TimeServer(chaoxi::net::EventLoop* loop,
               const chaoxi::net::InetAddress& listenAddr);
    void start();  // calls server_.start();

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn);

    // void onMessage(const chaoxi::net::TcpConnectionPtr& conn,
    //                chaoxi::net::Buffer& buf,
    //                chaoxi::Timestamp time);

    chaoxi::net::EventLoop* loop_;
    chaoxi::net::TcpServer server_;
};
