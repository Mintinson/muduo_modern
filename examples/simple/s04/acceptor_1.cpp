#include "chaoxi/net/Acceptor.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <print>

#include <unistd.h>

void newConnection(int sockfd, const chaoxi::net::InetAddress& peerAddr) {
    std::println("newConnection(): accept a new connection from {}",
                 peerAddr.toIpPort());
    std::string msg = "How are you?\n";
    ::write(sockfd, msg.c_str(), msg.size());
    chaoxi::net::sockets::close(sockfd);
}

void newConnection2(int sockfd, const chaoxi::net::InetAddress& peerAddr) {
    std::println("newConnection(): accept a new connection from {}",
                 peerAddr.toIpPort());
    std::string msg = "What's up man!\n";
    ::write(sockfd, msg.c_str(), msg.size());
    chaoxi::net::sockets::close(sockfd);
}


int main() {
    std::println("main (): pid={}\n", getpid());

    chaoxi::net::InetAddress listenAddr(9981);

    chaoxi::net::EventLoop loop;

    chaoxi::net::Acceptor acceptor(&loop, listenAddr);
    acceptor.setNewConnectionCallback(newConnection);
    acceptor.listen();

    chaoxi::net::InetAddress listenAddr2(9999);
    chaoxi::net::Acceptor acceptor2(&loop, listenAddr2);
    acceptor2.setNewConnectionCallback(newConnection2);
    acceptor2.listen();

    loop.loop();
}