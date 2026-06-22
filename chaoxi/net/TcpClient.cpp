///
/// @file TcpClient.cpp
/// @brief TcpClient 实现 —— 主动连接服务器 + 自动重试/重连
///

#include "chaoxi/net/TcpClient.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Connector.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <format>

namespace chaoxi::net
{

///
/// @brief 构造 TcpClient —— 创建 Connector 但不立即连接
///
/// 构造时只创建 Connector 对象并设置其"连接成功"回调 → this->newConnection()。
/// 需要调用 connect() 才真正开始发起连接。
///
TcpClient::TcpClient(EventLoop* loop,
                     const InetAddress& serverAddr,
                     std::string nameArg)
    : loop_(loop)
    , connector_(std::make_shared<Connector>(loop, serverAddr))
    , name_(std::move(nameArg))
{
    // 连接成功 → 把 sockfd 包装成 TcpConnection
    connector_->setNewConnectionCallback([this](int sockfd)
                                         { newConnection(sockfd); });
    FLOG_INFO("TcpClient::TcpClient[{}] - connector {}", name_,
              (void*)connector_.get());
}

///
/// @brief 析构 TcpClient —— 断开连接并清理资源
///
/// 析构时的复杂处理：
///
///   情况 A: 有活跃连接 (connection_ 非空)
///     ├─ 如果 connection_ 只被当前 TcpClient 持有 (use_count == 1)
///     │   └─ 直接 forceClose()
///     │       └─ handleClose → closeCallback → removeConnection →
///     connectDestroyed ├─ 如果 connection_ 被其他地方也持有 │   └─ 设置
///     closeCallback 并在 EventLoop 中 connectDestroyed │
///     让其他持有者决定何时销毁 │
///   情况 B: 没有活跃连接
///     └─ connector_->stop() —— 停止 Connector，不再重试
///     └─ runAfter(1.0, [connector]) —— 延长 Connector 生命周期 1 秒
///         防止 Connector 正在回调中就被析构（优雅的 hack）
///
TcpClient::~TcpClient()
{
    FLOG_INFO("TcpClient::~TcpClient[{}] - connector {}", name_,
              (void*)connector_.get());

    TcpConnectionPtr conn;
    bool unique = false;
    {
        std::scoped_lock lock(mutex_);
        unique = (connection_.use_count() == 1);
        conn = connection_;
    }

    if (conn)
    {
        assert(loop_ == conn->getLoop());

        // 设置关闭回调：确保 connectDestroyed 在 EventLoop 线程中执行
        auto closeCallback = [loop = loop_](const TcpConnectionPtr& c)
        { loop->queueInLoop([c] { c->connectDestroyed(); }); };

        // 在 EventLoop 线程中设置回调（解决跨线程安全问题）
        loop_->runInLoop([conn, closeCallback = std::move(closeCallback)]
                         { conn->setCloseCallback(closeCallback); });

        // 如果我们是 connection_ 的唯一持有者，直接关闭
        if (unique)
        {
            conn->forceClose();
        }
    }
    else
    {
        // 没有活跃连接 → 停止 Connector
        connector_->stop();
        // Hack: 延迟 1 秒再销毁 Connector
        // 原因：Connector 可能正在计时器回调中，如果立即析构会导致问题。
        // 通过将 shared_ptr 捕获到定时器回调中，延长其生命周期。
        loop_->runAfter(1.0,
                        [connector = connector_]
                        {
                            // 闭包持有 shared_ptr，1 秒后自动析构
                        });
    }
}

///
/// @brief 发起连接
///
/// 调用 connector_->start() 开始非阻塞 connect + 自动重试。
///
void TcpClient::connect()
{
    FLOG_INFO("TcpClient::connect[{}] - connecting to {}", name_,
              connector_->serverAddress().toIpPort());
    connect_.store(true, std::memory_order_release);
    connector_->start();
}

///
/// @brief 断开连接（优雅关闭 —— shutdown 写端）
///
void TcpClient::disconnect()
{
    connect_.store(false, std::memory_order_release);
    {
        std::scoped_lock lock(mutex_);
        if (connection_)
        {
            connection_->shutdown();
        }
    }
}

///
/// @brief 完全停止（断开连接 + 停止 Connector，不再重连）
///
void TcpClient::stop()
{
    connect_.store(false, std::memory_order_release);
    connector_->stop();
}

///
/// @brief Connector 连接成功后的回调 —— 把 sockfd 包装成 TcpConnection
///
/// 调用时机：Connector 的 connect 非阻塞返回，且 poll 检测到该 fd 可写，
///           且 SO_ERROR 为 0（不是自连接），Connector 认为连接成功。
///
/// 步骤：
///   1. 获取对端和本地地址
///   2. 生成连接名称
///   3. 创建 TcpConnection 对象
///   4. 设置用户回调（connection / message / writeComplete / close）
///   5. 存入 connection_ 成员
///   6. 调用 connectEstablished() 完成激活
///
void TcpClient::newConnection(int sockfd)
{
    loop_->assertInLoopThread();

    InetAddress peerAddr{sockets::getPeerAddr(sockfd)};

    // 生成连接名称（格式：clientName:serverIP:port#1）
    std::string connName =
        std::format("{}:{}#{}", name_, peerAddr.toIpPort(), nextConnId_++);

    InetAddress localAddr{sockets::getLocalAddr(sockfd)};

    auto conn = std::make_shared<TcpConnection>(loop_, connName, sockfd,
                                                localAddr, peerAddr);

    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 连接断开时 → removeConnection（包含可选的自动重连逻辑）
    conn->setCloseCallback([this](const TcpConnectionPtr& c)
                           { removeConnection(c); });

    {
        std::scoped_lock lock(mutex_);
        connection_ = conn;
    }

    // 激活连接：state → kConnected, tie, enableReading, connectionCallback
    conn->connectEstablished();
}

///
/// @brief 连接断开时的清理 + 可选自动重连
///
/// 线程：EventLoop 线程。
///
/// 步骤：
///   1. 从 connection_ 中移除该连接（必要时重置 shared_ptr）
///   2. 在 EventLoop 中延迟调用 connectDestroyed()（queueInLoop
///   保证不在回调链中）
///   3. 如果 retry_ 为 true 且 connect_ 为 true → 调用 connector_->restart()
///   重连
///
void TcpClient::removeConnection(const TcpConnectionPtr& conn)
{
    loop_->assertInLoopThread();
    assert(loop_ == conn->getLoop());

    {
        std::scoped_lock lock(mutex_);
        assert(connection_ == conn);
        connection_.reset();  // 释放对连接的引用
    }

    // 延迟销毁（不在当前回调链中执行）
    loop_->queueInLoop([conn] { conn->connectDestroyed(); });

    // 自动重连逻辑
    if (retry_.load(std::memory_order_acquire) &&
        connect_.load(std::memory_order_acquire))
    {
        LOG_INFO << "TcpClient::connect[" << name_ << "] - Reconnecting to "
                 << connector_->serverAddress().toIpPort();
        // Connector::restart 重置状态并重新开始连接
        connector_->restart();
    }
}

}  // namespace chaoxi::net
