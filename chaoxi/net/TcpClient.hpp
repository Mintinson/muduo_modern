#pragma once

///
/// @file TcpClient.hpp
/// @brief TCP 客户端 —— 主动发起 TCP 连接
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║          TcpClient + Connector —— 主动连接 + 自动重试                 ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║  TcpClient 是 muduo 的"主动发起者"，它使用内部的 Connector 来：      ║
/// ║    1. 非阻塞 connect() 到服务器                                      ║
/// ║    2. connect 成功后把 fd 包装成 TcpConnection                        ║
/// ║    3. 如果 connect 失败，自动重试（指数退避：500ms → 1s → 2s...）    ║
/// ║    4. 连接断开后可选择自动重连                                        ║
/// ║                                                                      ║
/// ║  架构：                                                               ║
/// ║                                                                      ║
/// ║   TcpClient                                                           ║
/// ║   ├── Connector (shared_ptr)                                         ║
/// ║   │   ├── 非阻塞 connect()                                            ║
/// ║   │   ├── Channel (监控 connect fd 的可写事件)                        ║
/// ║   │   ├── 指数退避重试                                                ║
/// ║   │   └── 成功后回调 newConnectionCallback_(sockfd)                  ║
/// ║   │                                                                   ║
/// ║   └── TcpConnectionPtr (连接成功后创建)                               ║
/// ║       ├── 和 TcpServer 中的 TcpConnection 完全相同                    ║
/// ║       └── 断开后触发 removeConnection → 自动重连（如果 retry_=true） ║
/// ║                                                                      ║
/// ║  生命周期：                                                           ║
/// ║  ─────────                                                           ║
/// ║                                                                      ║
/// ║  TcpClient() ──▶ connect() ──▶ Connector::start() ──▶ 非阻塞 connect ║
/// ║                                                                      ║
/// ║      成功: newConnection(sockfd) → TcpConnection → connectEstablished║
/// ║      失败: retry (500ms → 1s → 2s → ... → 30s)                       ║
/// ║                                                                      ║
/// ║      断开: closeCallback → removeConnection → connectDestroyed        ║
/// ║            → 如果 retry_=true → Connector::restart() → 重新连接      ║
/// ║                                                                      ║
/// ║  ~TcpClient() ──▶ stop() ──▶ forceClose / stop connector            ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝

#include "chaoxi/net/TcpConnection.hpp"

#include <memory>
#include <mutex>

namespace chaoxi::net
{
class Connector;

using ConnectorPtr = std::shared_ptr<Connector>;

class TcpClient
{
public:
    ///
    /// @brief 构造 TcpClient（但还不发起连接）
    ///
    /// @param loop       所属 EventLoop（所有操作在此线程执行）
    /// @param serverAddr 服务器地址
    /// @param nameArg    客户端名称
    ///
    TcpClient(EventLoop* loop,
              const InetAddress& serverAddr,
              std::string nameArg);
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    TcpClient(TcpClient&&) = delete;
    TcpClient& operator=(TcpClient&&) = delete;

    /// @brief 发起连接（内部调用 Connector::start）
    void connect();
    /// @brief 断开当前连接（shutdown 写端）
    void disconnect();
    /// @brief 完全停止（停止 Connector，不再重连）
    void stop();

    /// @brief 获取当前连接的 shared_ptr（线程安全）
    TcpConnectionPtr connection() const
    {
        std::scoped_lock lock(mutex_);
        return connection_;
    }

    EventLoop* getLoop() const { return loop_; }

    bool retry() const { return retry_.load(std::memory_order_acquire); }

    /// @brief 启用自动重连（连接断开后自动尝试重新连接）
    void enableRetry() { retry_.store(true, std::memory_order_release); }

    const std::string& name() const noexcept { return name_; }

    /// 设置连接回调（建立/断开）
    void setConnectionCallback(ConnectionCallback cb) noexcept
    {
        connectionCallback_ = std::move(cb);
    }

    /// 设置消息回调
    void setMessageCallback(MessageCallback cb) noexcept
    {
        messageCallback_ = std::move(cb);
    }

    /// 设置写完成回调
    void setWriteCompleteCallback(WriteCompleteCallback cb) noexcept
    {
        writeCompleteCallback_ = std::move(cb);
    }

private:
    /// Connector 成功 connect 后的回调（在 EventLoop 线程执行）
    void newConnection(int sockfd);

    /// 连接关闭时的回调（在 EventLoop 线程执行）
    void removeConnection(const TcpConnectionPtr& conn);

    EventLoop* loop_;
    ConnectorPtr
        connector_;  ///< 连接器（shared_ptr，可能在定时器中延长生命周期）
    const std::string name_;  ///< 客户端名称
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    std::atomic<bool> retry_{false};   ///< 是否断开后自动重连
    std::atomic<bool> connect_{true};  ///< 是否允许发起连接

    int nextConnId_{1};            ///< 连接 ID 计数器（仅在 EventLoop 线程访问）
    mutable std::mutex mutex_;     ///< 保护 connection_ 的跨线程访问
    TcpConnectionPtr connection_;  ///< 当前连接（若有）
};
}  // namespace chaoxi::net
