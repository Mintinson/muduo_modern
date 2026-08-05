#include "chaoxi/net/Connector.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/SocketOps.hpp"
#include "chaoxi/net/TcpClient.hpp"

#include <iostream>
#include <memory>
#include <print>

#ifndef _WIN32
#include <sys/socket.h>
#endif

chaoxi::net::EventLoop* g_loop;

void connectCallback(chaoxi::net::SocketHandle sockfd)
{
    std::cout << "Connected to sockfd " << sockfd << std::endl;
    auto peerAddress = chaoxi::net::sockets::getPeerAddr(sockfd);
    std::println("address {}", chaoxi::net::sockets::toIpPort(
                                   reinterpret_cast<sockaddr*>(&peerAddress)));
    g_loop->quit();
}

int main()
{
    chaoxi::net::EventLoop loop;

    g_loop = &loop;
    chaoxi::net::InetAddress addr("127.0.0.1", 10275);
    auto connector = std::make_shared<chaoxi::net::Connector>(&loop, addr);
    connector->setNewConnectionCallback(connectCallback);

    connector->start();

    loop.loop();
}
