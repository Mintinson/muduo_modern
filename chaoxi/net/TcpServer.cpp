///
/// @file TcpServer.cpp
/// @brief TcpServer 实现 —— accept → 创建连接 → 分发到 io 线程 → 管理生命周期
///

#include "chaoxi/net/TcpServer.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Acceptor.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThreadPool.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/SocketOps.hpp"
#include "chaoxi/net/TcpConnection.hpp"

#include <format>
#include <memory>

namespace chaoxi::net
{

///
/// @brief 构造 TcpServer —— 创建 Acceptor 和 ThreadPool
///
/// 参数说明：
///   @param loop       主 EventLoop（acceptor 线程）
///   @param listenAddr 监听地址（如 0.0.0.0:9981）
///   @param name       服务器名称
///   @param option     kNoReusePort 或 kReusePort
///
/// 构造时事务：
///   1. 创建 Acceptor —— 内部包含 listening socket + listen Channel
///      - Acceptor 构造时完成 socket() + bind() + listen()
///      - Channel 已注册到 Poller（但尚未启用读事件，等待 start() 中调用
///      listen()）
///   2. 设置 Acceptor 的新连接回调 → 指向 this->newConnection()
///   3. 创建 EventLoopThreadPool（此时尚未启动）
///
TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     std::string name,
                     Option option)
    : loop_(loop)
    , ipPort_(listenAddr.toIpPort())
    , name_(std::move(name))
    , acceptor_(std::make_unique<Acceptor>(
          loop, listenAddr, option == Option::kReusePort))
    , threadPool_(std::make_shared<EventLoopThreadPool>(loop, name_))
{
    // 当有新连接被 accept 时，Acceptor 会调用此 lambda
    acceptor_->setNewConnectionCallback(
        [this](int sockfd, const InetAddress& peerAddr)
        { newConnection(sockfd, peerAddr); });
}

///
/// @brief 析构 TcpServer —— 通知所有活跃连接销毁
///
/// 必须在 acceptor 线程中调用（由 EventLoop 保证）。
///
/// 析构流程：
///   1. 遍历 connections_ map 中的所有连接
///   2. 对每个连接，在其所属的 io loop 中调用 connectDestroyed()
///      （这会将 Channel 从 Poller 中移除，确保安全析构）
///   3. 将 conn 从 connections_ map 中重置
///
/// 为什么 iterate 和 conn.reset 要分开？
///   TcpConnectionPtr localConn = conn;  // 延长生命周期
///   conn.reset();                       // 释放 map 中的引用
///   localConn->getLoop()->runInLoop([localConn]{ connectDestroyed(); });
///   这样确保即使 connectDestroyed 的回调中访问 connections_，也不会死锁。
///
TcpServer::~TcpServer()
{
    loop_->assertInLoopThread();
    LOG_TRACE << std::format("TcpServer::~TcpServer [{}] destructing", name_);

    for (auto& [name, conn] : connections_)
    {                                       // C++17 结构化绑定
        TcpConnectionPtr localConn = conn;  // 延长生命周期
        conn.reset();                       // 释放 map 中的引用
        // 在连接所属的 io loop 中执行 connectDestroyed
        localConn->getLoop()->runInLoop([localConn]
                                        { localConn->connectDestroyed(); });
    }
}

///
/// @brief 设置 io 线程数量
///
/// 必须在 start() 之前调用。
///
void TcpServer::setThreadNum(int numThreads)
{
    assert(0 <= numThreads);
    threadPool_->setThreadNum(numThreads);
}

///
/// @brief 启动服务器
///
/// 使用 std::atomic<bool>::exchange 保证只执行一次（线程安全）。
///
/// 启动时做的事：
///   1. 启动线程池（创建所有 io 线程，每个进入 loop.loop()）
///      └─ start(initCb) 不是线程安全的，但 start() 的 exchange 保证了单次调用
///   2. 告诉 Acceptor 开始监听可读事件（listen Channel 启用 POLLIN）
///      └─ acceptor_->listen() 内部调用 acceptChannel_.enableReading()
///      └─ 从此刻起，有新连接到达时 poll 会检测到并触发 Acceptor::handleRead
///
void TcpServer::start()
{
    // exchange: 原子地设置 started_ = true，返回旧值
    // 如果旧值是 false（第一次调用）→ 执行启动逻辑
    // 如果旧值是 true（重复调用）→ 什么都不做
    if (!started_.exchange(true, std::memory_order_acq_rel))
    {
        threadPool_->start(threadInitCallback_);  // 启动所有 io 线程

        assert(!acceptor_->listening());
        // 在 acceptor 线程中启用监听
        loop_->runInLoop([this] { acceptor_->listen(); });
    }
}

///
/// @brief 处理新连接 —— TcpServer 最核心的方法
///
/// 被 Acceptor 在 accept 线程中回调。
///
/// 完整流程：
///   1. round-robin 选择一个 io loop
///   2. 生成连接名称（"serverName-0.0.0.0:9981#1"）
///   3. 创建 TcpConnection 对象（shared_ptr）
///   4. 设置四个回调（connection / message / writeComplete / close）
///   5. 存入 connections_ map
///   6. 在 io loop 线程中调用 conn->connectEstablished() 完成激活
///
/// 线程安全：
///   - 本方法在 acceptor 线程执行
///   - connectEstablished 通过 runInLoop 编入 io 线程执行
///   - connections_ 的修改在 acceptor 线程，读取也在 acceptor 线程
///
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
    loop_->assertInLoopThread();

    // ① 选择一个 io loop（如果 numThreads==0，返回 baseLoop_）
    EventLoop* ioLoop = threadPool_->getNextLoop();

    // ② 生成连接名
    std::string connName =
        std::format("{}-{}#{}", name_, ipPort_, nextConnId_++);

    FLOG_INFO("TcpServer::newConnection [{}] - new connection [{}] from {}",
              name_, connName, peerAddr.toIpPort());

    // ③ 获取本地地址
    InetAddress localAddr(sockets::getLocalAddr(sockfd));

    // ④ 创建 TcpConnection（shared_ptr，自动管理生命周期）
    auto conn = std::make_shared<TcpConnection>(ioLoop, connName, sockfd,
                                                localAddr, peerAddr);

    // ⑤ 存入连接表（保证 shared_ptr 不被销毁）
    connections_[connName] = conn;

    // ⑥ 设置用户回调
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // ⑦ 设置关闭回调 → TcpServer 负责清理
    conn->setCloseCallback([this](const TcpConnectionPtr& c)
                           { removeConnection(c); });

    // ⑧ 在 io 线程中完成连接激活
    //    connectEstablished: state→kConnected, tie, enableReading,
    //    connectionCallback
    ioLoop->runInLoop([conn] { conn->connectEstablished(); });
}

///
/// @brief 移除连接（跨线程安全入口）
///
/// 由 TcpConnection::closeCallback_ 调用（在 io loop 线程），
/// 通过 runInLoop 转到 acceptor 线程执行 removeConnectionInLoop。
///
void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
    loop_->runInLoop([this, conn] { removeConnectionInLoop(conn); });
}

///
/// @brief 在 acceptor 线程中实际执行连接移除
///
/// 步骤：
///   1. 从 connections_ map 中删除该连接（最后一个 shared_ptr 被移除？不一定）
///   2. 在 io loop 线程中调用 conn->connectDestroyed()
///      └─ 使用 queueInLoop（而非 runInLoop）确保不在回调链中执行
///      └─ connectDestroyed 会 disableAll + remove Channel
///
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
    loop_->assertInLoopThread();
    LOG_INFO << std::format(
        "TcpServer::removeConnectionInLoop [{}] - connection {}", name_,
        conn->name());

    // 从连接表中移除
    size_t n = connections_.erase(conn->name());
    assert(n == 1);

    // 在 io 线程中调用 connectDestroyed（清理 Channel）
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop([conn] { conn->connectDestroyed(); });
}

}  // namespace chaoxi::net
