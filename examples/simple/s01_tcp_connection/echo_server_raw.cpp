///
/// @file examples/simple/s01_tcp_connection/echo_server_raw.cpp
/// @brief 手工打造的 Echo Server —— 展示 TcpConnection 的完整生命周期
///
/// 本示例不用 TcpServer，而是手工使用 EventLoop + Channel + TcpConnection
/// 来搭建一个 Echo 服务器，帮助理解 TcpConnection 在真实场景中的角色。
///
/// 工作流程：
///
///   1. 在 9981 端口创建监听 socket，包装成 listenChannel_
///   2. listenChannel_ 被 Poller 监听 POLLIN
///   3. 当有新连接到达时，accept 得到新 fd
///   4. 把新 fd 包装成 TcpConnection，设置回调，connectEstablished() 开始服务
///   5. 收到数据时 messageCallback 把数据原样发回（echo）
///   6. 对端关闭或出错时 handleClose 自动清理连接
///
/// 使用方法：
///   编译后运行 ./echo_server_raw
///   然后在另一个终端: nc localhost 9981
///   输入任意文字会原样回显，Ctrl+D 断开
///

#include "chaoxi/net/TcpConnection.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/Buffer.hpp"
#include "chaoxi/net/Socket.hpp"
#include "chaoxi/net/SocketOps.hpp"
#include "chaoxi/base/Timestamp.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <print>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace chaoxi::net;

///
/// @brief 手工 Echo 服务器 —— 不使用 TcpServer，展示底层原理
///
class RawEchoServer {
public:
    RawEchoServer(EventLoop* loop, const InetAddress& listenAddr)
        : loop_(loop)
        , listenSocket_(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0))
        , listenChannel_(loop, listenSocket_.fd()) {

        assert(listenSocket_.fd() >= 0);

        // 设置 SO_REUSEADDR，避免重启时 "Address already in use"
        listenSocket_.setReuseAddr(true);
        // 绑定监听地址
        listenSocket_.bindAddress(listenAddr);
        // 开始监听（backlog = SOMAXCONN）
        listenSocket_.listen();

        // 设置监听 fd 的可读回调：当有新连接时，调用 onAccept
        listenChannel_.setReadCallback([this](chaoxi::Timestamp) { onAccept(); });
        listenChannel_.enableReading();

        std::println("EchoServer listening on {}",
                     listenAddr.toIpPort());
    }

private:
    ///
    /// @brief 当监听 fd 可读时被调用 —— 表示有新连接到达
    ///
    void onAccept() {
        InetAddress peerAddr;  // accept 会把客户端地址填进去
        int connfd = listenSocket_.accept(&peerAddr);

        if (connfd >= 0) {
            std::string connName = std::format("conn-{}", nextConnId_++);
            InetAddress localAddr{sockets::getLocalAddr(connfd)};

            std::println("new connection [{}] from {} -> {}",
                         connName, peerAddr.toIpPort(), localAddr.toIpPort());

            // ################################################################
            // 核心步骤：把新 accept 的 fd 包装成 TcpConnection
            // ################################################################
            TcpConnectionPtr conn = std::make_shared<TcpConnection>(
                loop_, connName, connfd, localAddr, peerAddr);

            // 设置消息回调：收到数据 → 原样发回（echo）
            conn->setMessageCallback(
                [](const TcpConnectionPtr& c, Buffer& buf, chaoxi::Timestamp) {
                    // 把收到的数据发回去
                    auto data = buf.readableSpan();
                    c->send(std::string_view{data.data(), data.size()});
                    buf.retrieveAll();
                });

            // 设置连接回调：连接断开时打印日志，并移除连接
            conn->setConnectionCallback(
                [this](const TcpConnectionPtr& c) {
                    if (c->connected()) {
                        std::println("connection [{}] established", c->name());
                    } else {
                        std::println("connection [{}] destroyed", c->name());
                        // 从 map 中移除
                        connections_.erase(std::string(c->name()));
                        // 必须调用 connectDestroyed，确保 Channel 从 Poller 中移除
                        // 注意：这里不能立即调用，因为回调链还在进行中
                        // 实际 muduo 中用 queueInLoop 延迟执行
                        c->getLoop()->queueInLoop(
                            [conn = c]() { conn->connectDestroyed(); });
                    }
                });

            // 把连接存入 map（保持 shared_ptr 存活）
            connections_[connName] = conn;

            // ################################################################
            // 确认连接建立：状态 → kConnected，开始监听读事件
            // ################################################################
            conn->connectEstablished();

        } else {
            // accept 失败（通常是资源耗尽）
            std::println("accept failed (errno={})", errno);
        }
    }

    EventLoop* loop_;
    Socket listenSocket_;
    Channel listenChannel_;
    std::map<std::string, TcpConnectionPtr> connections_;
    int nextConnId_{1};
};

int main() {
    EventLoop loop;

    // 监听 0.0.0.0:9981
    InetAddress listenAddr(9981, false);  // false = 不 loopbackOnly，监听所有 IP
    RawEchoServer server(&loop, listenAddr);

    std::println("Entering event loop (press Ctrl+C to exit)...");
    loop.loop();

    return 0;
}
