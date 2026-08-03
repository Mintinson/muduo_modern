#pragma once

///
/// @file TcpServer.hpp
/// @brief TCP 服务器 —— muduo 网络库对外的"正面大门"
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║               TcpServer —— 把一切组装起来的"总指挥"                  ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║  TcpServer 是 muduo 中最"高层"的组件，它把前面所有组件串联起来：     ║
/// ║                                                                      ║
/// ║   ┌────────────┐                                                     ║
/// ║   │  TcpServer │  ← 用户直接使用的接口                               ║
/// ║   └─────┬──────┘                                                     ║
/// ║         │                                                            ║
/// ║    ┌────┴─────┬──────────────┬──────────────┐                        ║
/// ║    ▼          ▼              ▼              ▼                        ║
/// ║  Acceptor  EventLoopThread  TcpConnection  回调                    ║
/// ║  (监听)    Pool (IO线程池)  (连接对象)     (用户设置)               ║
/// ║    │          │              │                                       ║
/// ║    ▼          ▼              ▼                                       ║
/// ║  Channel   EventLoop      Channel                                   ║
/// ║  (listen)  (每个线程)     (每个连接)                                 ║
/// ║    │          │              │                                       ║
/// ║    └──────────┴──────────────┘                                       ║
/// ║               │                                                      ║
/// ║               ▼                                                      ║
/// ║            Poller (poll/epoll)                                       ║
/// ║                                                                      ║
/// ║  从 accept 到数据处理的完整链路：                                     ║
/// ║  ───────────────────────────────                                     ║
/// ║                                                                      ║
/// ║  1. 客户端连接到达                                                    ║
/// ║     └→ Poller 检测到 listening fd 可读                                ║
/// ║     └→ Acceptor::handleRead → ::accept() 得到新 fd                   ║
/// ║     └→ 回调 TcpServer::newConnection(sockfd, peerAddr)               ║
/// ║                                                                      ║
/// ║  2. TcpServer::newConnection 的处理                                   ║
/// ║     └→ threadPool_->getNextLoop() 选一个 io loop                      ║
/// ║     └→ 创建 TcpConnection 对象                                        ║
/// ║     └→ 设置用户回调（connection/message/writeComplete）              ║
/// ║     └→ 存入 connections_ map                                         ║
/// ║     └→ ioLoop->runInLoop([conn]{ conn->connectEstablished(); })      ║
/// ║        └→ 连接在 io 线程中完成最终激活                                 ║
/// ║                                                                      ║
/// ║  3. 数据到达时                                                       ║
/// ║     └→ TcpConnection::handleRead → messageCallback_(conn, buf, time)  ║
/// ║     └→ 用户的 messageCallback 被调用                                  ║
/// ║                                                                      ║
/// ║  4. 连接断开时                                                       ║
/// ║     └→ TcpConnection::handleClose → closeCallback_(conn)              ║
/// ║     └→ TcpServer::removeConnection → 从 connections_ map 删除         ║
/// ║     └→ conn->connectDestroyed() → Channel::remove → 安全析构         ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝

#include "chaoxi/base/Utility.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/TcpConnection.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace chaoxi::net
{

class Acceptor;
class EventLoop;
class EventLoopThreadPool;

class TcpServer
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    /// SO_REUSEPORT 选项（允许多进程/线程 bind 同一端口做负载均衡）
    enum class Option : std::uint8_t
    {
        kNoReusePort,
        kReusePort,
    };

    ///
    /// @brief 构造 TcpServer
    ///
    /// @param loop       主 EventLoop（acceptor 线程），必须非空
    /// @param listenAddr 监听地址（如 InetAddress(9980)）
    /// @param name       服务器名称（用于日志和连接名生成）
    /// @param option     SO_REUSEPORT 开关
    ///
    /// 构造时做的事情：
    ///   1. 创建 Acceptor（绑定 + 监听 + 设置 accept 回调为
    ///   this->newConnection）
    ///   2. 创建 EventLoopThreadPool
    ///
    TcpServer(EventLoop* loop,
              const InetAddress& listenAddr,
              std::string name,
              Option option = Option::kNoReusePort);
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;
    ~TcpServer();

    [[nodiscard]] const std::string& ipPort() const noexcept { return ipPort_; }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    [[nodiscard]] EventLoop* getLoop() const noexcept { return loop_; }

    /// @brief 设置 io 线程数量（必须在 start() 前调用）
    /// @param numThreads
    ///   - 0: 所有 I/O 在 acceptor 线程中处理（默认，单线程模式）
    ///   - 1: 所有 I/O 在独立的一个 io 线程中处理
    ///   - N: 创建 N 个 io 线程，新连接被 round-robin 分配
    void setThreadNum(unsigned numThreads);

    /// @brief 设置线程初始化回调（每个 io 线程启动时调用）
    void setThreadInitCallback(ThreadInitCallback cb)
    {
        threadInitCallback_ = std::move(cb);
    }

    /// @brief 获取线程池 shared_ptr（可用于外部访问）
    [[nodiscard]] std::shared_ptr<EventLoopThreadPool> threadPool()
        const noexcept
    {
        return threadPool_;
    }

    /// @brief 启动服务器（Acceptor 开始 listen）
    /// 可以多次调用（内部用 std::atomic exchange 保证只执行一次）
    void start();

    /// @brief 设置连接回调（连接建立/断开时调用）
    void setConnectionCallback(ConnectionCallback cb) noexcept
    {
        connectionCallback_ = std::move(cb);
    }

    /// @brief 设置消息回调（收到数据时调用）
    void setMessageCallback(MessageCallback cb) noexcept
    {
        messageCallback_ = std::move(cb);
    }

    void setWriteCompleteCallback(WriteCompleteCallback cb) noexcept
    {
        writeCompleteCallback_ = std::move(cb);
    }

private:
    ///
    /// @brief Acceptor 回调 —— 当新连接到达时被调用
    ///
    /// 由 Acceptor::handleRead → accept → newConnectionCallback_ 调用。
    /// 线程：acceptor 所在 EventLoop 线程。
    ///
    void newConnection(SocketHandle sockfd, const InetAddress& peerAddr);

    /// 连接断开时由 TcpConnection::closeCallback_ 回调（跨线程安全版本）
    void removeConnection(const TcpConnectionPtr& conn);

    /// 在 acceptor 线程中执行实际的删除（从 map 移除 + connectDestroyed）
    void removeConnectionInLoop(const TcpConnectionPtr& conn);

    /// 连接表：connName → shared_ptr<TcpConnection>
    using ConnectionMap = std::unordered_map<std::string,
                                             TcpConnectionPtr,
                                             base::StringHash,
                                             std::equal_to<>>;

    EventLoop* loop_;           ///< acceptor 线程的 EventLoop（主 Reactor）
    const std::string ipPort_;  ///< 监听地址的字符串表示
    const std::string name_;    ///< 服务器名称
    std::unique_ptr<Acceptor>
        acceptor_;  ///< 监听器（内部持有 Channel + Socket）
    std::shared_ptr<EventLoopThreadPool> threadPool_;  ///< IO 线程池
    ConnectionCallback connectionCallback_;            ///< 用户：连接状态回调
    MessageCallback messageCallback_;                  ///< 用户：消息回调
    WriteCompleteCallback writeCompleteCallback_;      ///< 用户：写完成回调
    ThreadInitCallback threadInitCallback_;            ///< 用户：线程初始化回调

    std::atomic<bool> started_{false};  ///< 是否已启动（防止重复启动）
    int nextConnId_ = 1;  ///< 连接 ID 计数器（仅在 acceptor 线程访问）

    ConnectionMap connections_;  ///< 所有活跃连接
};
}  // namespace chaoxi::net
