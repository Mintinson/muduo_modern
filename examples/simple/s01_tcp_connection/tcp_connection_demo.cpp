///
/// @file examples/simple/s01_tcp_connection/tcp_connection_demo.cpp
/// @brief TcpConnection 最小使用示例 —— 用 socketpair 模拟一个 TCP 连接
///
/// 本示例演示 TcpConnection 的最基本生命周期：
///   1. 用 socketpair 创建一对已连接的 fd（模拟 TCP 连接）
///   2. 把一端包装成 TcpConnection
///   3. 设置消息回调 → connectEstablished → 收发数据 → 关闭
///
/// 不需要网络，不需要 TcpServer/TcpClient，最纯粹的 TcpConnection 演示。
///
/// 编译：g++ -std=c++23 tcp_connection_demo.cpp -lchaoxi_net -lchaoxi_base
/// 运行：./tcp_connection_demo
///

#include "chaoxi/net/TcpConnection.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/Buffer.hpp"

#include <cstring>
#include <iostream>
#include <print>

#include <sys/socket.h>
#include <unistd.h>

using namespace chaoxi::net;

int main() {
    // ========================================================================
    // Step 1: 用 socketpair 创建一对已连接的 fd
    // ========================================================================
    // socketpair 返回一对已连接的 Unix domain socket fd
    //   fds[0] — 我们手动操作的一侧（模拟"客户端"）
    //   fds[1] — 包装成 TcpConnection 的一侧（模拟"服务端连接"）
    //
    // 示意图:
    //   main() ──write──▶ fds[0] ═══════ fds[1] ──read──▶ TcpConnection
    //                          (内核中连接)
    //
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        std::perror("socketpair");
        return 1;
    }

    // ========================================================================
    // Step 2: 创建 EventLoop
    // ========================================================================
    EventLoop loop;

    // ========================================================================
    // Step 3: 把 fds[1] 包装成 TcpConnection
    // ========================================================================
    // TcpConnection 接管 fds[1] 的所有权：连接断开后会自动 close(fds[1])
    //
    InetAddress localAddr(0, true);   // 127.0.0.1:0
    InetAddress peerAddr(0, true);    // 127.0.0.1:0
    TcpConnectionPtr conn = std::make_shared<TcpConnection>(
        &loop, "demo_conn", fds[1], localAddr, peerAddr);

    std::println("TcpConnection created: name={}, state=kConnecting",
                 conn->name());

    // ========================================================================
    // Step 4: 设置消息回调 —— 当收到数据时被调用
    // ========================================================================
    // 回调签名: void(shared_ptr<TcpConnection>, Buffer&, Timestamp)
    //
    conn->setMessageCallback(
        [](const TcpConnectionPtr& c, Buffer& buf, chaoxi::Timestamp receiveTime) {
            std::string msg = buf.retrieveAllAsString();
            std::println("Received: \"{}\" ({} bytes)", msg, msg.size());

            // 收到消息后立刻退出循环（只演示一次收发）
            c->getLoop()->quit();
        });

    // ========================================================================
    // Step 5: 连接建立确认 —— 相当于 TcpServer 调用 conn->connectEstablished()
    // ========================================================================
    // 这一步做了：
    //   1. state_ → kConnected
    //   2. channel_->tie(shared_from_this()) — 防止回调时连接已析构
    //   3. channel_->enableReading() — 开始监听读事件
    //   4. 触发 connectionCallback_
    //
    conn->connectEstablished();
    std::println("Connection established. State=kConnected");

    // ========================================================================
    // Step 6: 从"对端"发送数据
    // ========================================================================
    std::string message = "Hello from the other side!";
    ssize_t n = ::write(fds[0], message.data(), message.size());
    std::println("Sent {} bytes via fds[0]: \"{}\"", n, message);

    // ========================================================================
    // Step 7: 运行事件循环（阻塞）
    // ========================================================================
    // poll 检测到 fds[1] 可读 → handleRead → messageCallback → quit()
    // quit() 设置 quit_ = true → while 条件不满足 → loop() 返回
    //
    std::println("Entering event loop...");
    loop.loop();
    std::println("Event loop exited.");

    // ========================================================================
    // Step 8: 清理
    // ========================================================================
    // 模拟 TcpServer 的清理流程
    conn->connectDestroyed();
    ::close(fds[0]);  // 关闭"客户端"侧

    std::println("Done. Connection destroyed, all fds closed.");
    return 0;
}
