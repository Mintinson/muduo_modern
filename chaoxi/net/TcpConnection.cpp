///
/// @file TcpConnection.cpp
/// @brief TcpConnection 实现 —— TCP 连接对象的核心逻辑
///
/// 本文讲解 TcpConnection 中每个方法的职责、调用时机、线程安全考量。
///

#include "chaoxi/net/TcpConnection.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/Socket.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <format>
#include <memory>
#include <string_view>

#include <sys/types.h>
#include <unistd.h>

namespace chaoxi::net
{

// ============================================================================
// 默认回调（占位）
// ============================================================================

///
/// @brief 默认的连接回调：打印连接的建立/断开日志
///
/// 如果用户没有设置 ConnectionCallback，就会调用这个默认版本。
/// 输出格式: "127.0.0.1:9981 -> 192.168.1.100:54321 is UP/DOWN"
///
void defaultConnectionCallback(const TcpConnectionPtr& conn)
{
    LOG_INFO << std::format("{} -> {} is {}", conn->localAddress().toIpPort(),
                            conn->peerAddress().toIpPort(),
                            (conn->connected() ? "UP" : "DOWN"));
}

///
/// @brief 默认的消息回调：直接丢弃收到的数据
///
/// 如果用户没有设置 MessageCallback，数据会被静默丢弃。
/// 防止在不关心消息的测试程序中缓冲区无限增长。
///
void defaultMessageCallback(const TcpConnectionPtr&, Buffer& buf, Timestamp)
{
    buf.retrieveAll();  // 丢弃所有可读数据
}

// ============================================================================
// 构造 / 析构
// ============================================================================

///
/// @brief TcpConnection 构造函数 —— 把一个已连接 socket fd 包装成连接对象
///
/// @param loop      所属 EventLoop
/// @param nameArg   连接名称（TcpServer 分配的如 "server#1"）
/// @param sockfd    已建立 TCP 连接的 fd（非阻塞 + close-on-exec）
/// @param localAddr 本端地址（accept 时获取）
/// @param peerAddr  对端地址（accept 时获取）
///
/// 构造时做的事情（按顺序）：
///
///   Step 1. socket_ = make_unique<Socket>(sockfd)
///           └─ Socket 对象接管 fd 的所有权，析构时 close(fd)
///
///   Step 2. channel_ = make_unique<Channel>(loop, sockfd)
///           └─ 把 fd 注册到 EventLoop 的 Poller（但还没启用事件）
///               此时 Poller 的 pollfds_ 里已有这个 fd，但 events 为 0
///
///   Step 3. channel_->setReadCallback([this](Timestamp t) { handleRead(t); })
///           channel_->setWriteCallback([this] { handleWrite(); })
///           channel_->setCloseCallback([this] { handleClose(); })
///           channel_->setErrorCallback([this] { handleError(); })
///           └─ 设置 Channel 的四个事件回调：
///                 可读 → handleRead (收数据)
///                 可写 → handleWrite (续写 outputBuffer_)
///                 关闭 → handleClose (对端断开或出错)
///                 错误 → handleError (socket 错误)
///
///   Step 4. socket_->setKeepAlive(true)
///           └─ 启用 TCP KeepAlive，防止死连接占用资源
///
/// @note 构造完成后，连接状态是 kConnecting，fd 已在 Poller 中但读事件未启用。
///       需要外部调用 connectEstablished() 来正式进入 kConnected 状态。
///
TcpConnection::TcpConnection(EventLoop* loop,
                             std::string nameArg,
                             int sockfd,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
    : loop_(loop)
    , name_(nameArg)
    , socket_(std::make_unique<Socket>(sockfd))          // ① 接管 fd
    , channel_(std::make_unique<Channel>(loop, sockfd))  // ② 创建 Channel
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
{
    // ③ 设置 Channel 的四个回调
    channel_->setReadCallback([this](Timestamp t) { handleRead(t); });
    channel_->setWriteCallback([this] { handleWrite(); });
    channel_->setCloseCallback([this] { handleClose(); });
    channel_->setErrorCallback([this] { handleError(); });
    LOG_DEBUG << std::format("TcpConnection::ctor[{}] at {} fd={}", name_,
                             (void*)this, sockfd);
    // ④ 启用 TCP KeepAlive
    socket_->setKeepAlive(true);
}

///
/// @brief 析构函数 —— 断言连接已完全断开
///
/// 为什么 assert(state_ == kDisconnected) 如此重要？
///   如果析构时 Channel 还在 Poller 的 pollfds_ 数组中（addedToLoop_ == true），
///   Channel 析构时会触发 assert(!addedToLoop_)。
///   因此必须在析构前调用 connectDestroyed()，它会执行 channel_->remove()。
///
TcpConnection::~TcpConnection()
{
    LOG_DEBUG << std::format("TcpConnection::dtor[{}] at {} fd={} state={}",
                             name_, (void*)this, channel_->fd(),
                             stateToString());
    assert(state_ == StateE::kDisconnected);
}

// ============================================================================
// TCP 信息查询
// ============================================================================

bool TcpConnection::getTcpInfo(struct tcp_info* tcpi) const noexcept
{
    return socket_->getTcpInfo(tcpi);
}

std::string TcpConnection::getTcpInfoString() const noexcept
{
    char buf[1024];
    buf[0] = '\0';
    (void)socket_->getTcpInfoString(buf, sizeof buf);
    return buf;
}

// ============================================================================
// handleRead —— 核心：接收数据 + 分发消息回调
// ============================================================================

///
/// @brief 处理 socket 可读事件 —— TcpConnection 最核心的方法之一
///
/// 调用链回顾:
///   ::poll() 返回 fd 可读
///     → EventLoop::loop() 遍历 activeChannels_
///     → channel_->handleEvent(receiveTime)
///     → Channel::handleEventWithGuard 检测到 POLLIN
///     → readCallback_(receiveTime)  ← 即此方法
///
/// 三个分支:
///   n > 0  → 读到数据，调用 messageCallback_(conn, buf, time) 通知用户
///   n == 0 → 对端关闭了连接 (EOF)，触发 handleClose()
///   n < 0  → 读取出错，记录日志后触发 handleError()
///
/// @param receiveTime poll 返回的时刻，传递给用户回调用于时间戳
///
void TcpConnection::handleRead(Timestamp receiveTime) noexcept
{
    // 断言必须在 EventLoop 线程中执行
    loop_->assertInLoopThread();
    int savedErrno = 0;

    // 从 socket 读数据到 inputBuffer_
    // readFd 内部使用 readv(2):
    //   - 优先读入 Buffer 的可写空间
    //   - 如果一次读不完，用栈上的 extrabuf (65536字节) 兜底，再 append 到
    //   Buffer
    //   - 这样避免了数据量大时的 Buffer 扩容开销
    auto n = inputBuffer_.readFd(channel_->fd(), &savedErrno);

    if (n > 0)
    {
        // ================================================================
        // 正常读取：数据已存入 inputBuffer_，通知用户处理
        // ================================================================
        // shared_from_this() 确保在用户回调执行期间连接对象不会被销毁
        // messageCallback_ 的签名: void(shared_ptr<TcpConnection>, Buffer&,
        // Timestamp)
        if (messageCallback_)
        {
            messageCallback_(shared_from_this(), inputBuffer_, receiveTime);
        }
    }
    else if (n == 0)
    {
        // ================================================================
        // read 返回 0 表示对端关闭了连接（TCP FIN）
        // ================================================================
        handleClose();
    }
    else
    {
        // ================================================================
        // read 返回 -1 表示出错（非 EAGAIN 的致命错误）
        // ================================================================
        errno = savedErrno;
        LOG_SYSERR << "TcpConnection::handleRead";
        handleError();
    }
}

// ============================================================================
// handleWrite —— 续写 outputBuffer_ 到 socket
// ============================================================================

///
/// @brief 处理 socket 可写事件 — 把 outputBuffer_ 中的待发数据写入 socket
///
/// 触发时机：
///   当 sendInLoop 中 write 没有一次写完（EAGAIN），剩余数据存入 outputBuffer_，
///   并启用写事件。当 socket 再次变为可写时，此方法被 poll → Channel 调用。
///
/// 执行逻辑：
///   1. 如果 outputBuffer_ 中没有待发数据 → 说明是异常情况，直接返回
///   2. 尝试将 outputBuffer_ 中的所有可读数据 write 到 socket
///   3. 全部写完：
///      - 关闭写事件监听（节省 poll 开销）
///      - 触发 writeCompleteCallback_（告知用户数据已全部发送）
///   4. 没写完：
///      - outputBuffer_ 中保留剩余数据，等待下次可写事件
///
void TcpConnection::handleWrite() noexcept
{
    loop_->assertInLoopThread();

    // 如果有数据待发送，尝试写入
    if (channel_->isWriting())
    {
        auto data = outputBuffer_.readableSpan();
        ssize_t n = sockets::write(channel_->fd(), data.data(), data.size());
        if (n > 0)
        {
            outputBuffer_.retrieve(static_cast<size_t>(n));
            // 全部发送完成
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();  // 关闭写事件监听
                if (writeCompleteCallback_)
                {
                    // queueInLoop 确保执行时状态一致
                    loop_->queueInLoop([conn = shared_from_this(),
                                        cb = writeCompleteCallback_]()
                                       { cb(conn); });
                }
                // 如果正在关闭中（shutdown
                // 时还有数据没发完），现在可以安全关闭了
                if (state_ == StateE::kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            // write 失败（不是 EAGAIN 的致命错误）
            LOG_SYSERR << "TcpConnection::handleWrite";
        }
    }
    else
    {
        LOG_TRACE << std::format("Connection fd = {} is down, no more writing",
                                 channel_->fd());
    }
}

// ============================================================================
// send —— 发送数据的跨线程安全入口
// ============================================================================

///
/// @brief 发送字符串数据（用户接口，可从任意线程调用）
///
/// 线程安全策略：
///   - 如果调用者在 EventLoop 线程 → 直接执行 sendInLoop()
///   - 如果调用者在其他线程 → 通过 runInLoop 编入 EventLoop 线程
///     └─ lambda 中用 shared_from_this() 持有连接 → 确保执行时连接存活
///     └─ std::string(message) 拷贝数据 → 确保数据在跨线程后有效
///
/// 状态检查：只有 kConnected 状态才接受发送，否则静默丢弃。
///
void TcpConnection::send(std::string_view message)
{
    // acquire 语义：与 setState 的 release 配对，确保读到最新的 state_
    if (state_.load(std::memory_order_acquire) == StateE::kConnected)
    {
        if (loop_->isInLoopThread())
        {
            // 已经在 EventLoop 线程，直接执行
            sendInLoop(message);
        }
        else
        {
            // 跨线程：把数据和 shared_ptr 打包进 lambda，提交给 EventLoop
            // 注意：必须用 std::string(message) 做一次拷贝！
            // 因为 message 指向的数据可能在 lambda 执行前就被释放了
            loop_->runInLoop(
                [conn = shared_from_this(), str = std::string(message)]()
                { conn->sendInLoop(str); });
        }
    }
}

///
/// @brief 发送 Buffer 中的数据（移动语义版本）
///
/// 与 send(string_view) 的核心区别：
///   - Buffer 被 std::move 传入 lambda，避免了内存分配
///   - 跨线程时直接"转移"整个 Buffer 到 EventLoop 线程，零拷贝
///   - 如果在 EventLoop 线程，读取数据后清空 Buffer（不移动整个 Buffer）
///
void TcpConnection::send(Buffer&& buf)
{
    if (state_.load(std::memory_order_acquire) == StateE::kConnected)
    {
        if (loop_->isInLoopThread())
        {
            auto data = buf.readableSpan();
            sendInLoop({data.data(), data.size()});
            buf.retrieveAll();  // 清空，数据已在 sendInLoop 中处理
        }
        else
        {
            // 整个 Buffer 移动到 IO 线程，零内存分配开销
            loop_->runInLoop(
                [conn = shared_from_this(), b = std::move(buf)]() mutable
                {
                    auto data = b.readableSpan();
                    conn->sendInLoop({data.data(), data.size()});
                });
        }
    }
}

///
/// @brief 在 EventLoop 线程中实际执行发送
///
/// 发送策略（关键！）：
///
///   情况 A：outputBuffer_ 为空 && 没有等待写事件
///     └─ 直接 write(fd, data, len)
///         ├─ 全部写完 → 触发 writeCompleteCallback_ （如有）
///         └─ 没写完 → 剩余数据 append 到 outputBuffer_，启用写事件监听
///
///   情况 B：outputBuffer_ 不为空 || 正在等待写事件
///     └─ 直接 append 到 outputBuffer_（排队发送，保持数据顺序）
///         └─ 如果累计超过高水位 → 触发 highWaterMarkCallback_
///
///   设计精髓：
///     只在"outputBuffer_ 为空"时才尝试直接 write。
///     这保证了数据的 FIFO 顺序：先 append 到 buffer 的数据先发送。
///     如果 outputBuffer_ 已有数据，新数据直接追加，等 handleWrite 统一发送。
///
/// @param message 要发送的数据（已经过跨线程拷贝，此处安全）
///
void TcpConnection::sendInLoop(std::string_view message)
{
    loop_->assertInLoopThread();
    ssize_t nwrote = 0;
    size_t remaining = message.size();
    bool faultError = false;

    // 如果连接已断开，放弃发送
    if (state_.load(std::memory_order_acquire) == StateE::kDisconnected)
    {
        LOG_WARN << "disconnected, give up writing";
        return;
    }

    // 情况 A：outputBuffer_ 为空 → 尝试直接写
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = sockets::write(channel_->fd(), message.data(), message.size());
        if (nwrote >= 0)
        {
            remaining = message.size() - (std::size_t)nwrote;
            // 全部写完，且设置了写完成回调
            if (remaining == 0 && writeCompleteCallback_)
            {
                // queueInLoop（而非 runInLoop）：让当前正在处理的回调先执行完
                loop_->queueInLoop(
                    [conn = shared_from_this(), cb = writeCompleteCallback_]()
                    { cb(conn); });
            }
        }
        else
        {
            // write 返回 -1
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                // 不是"缓冲区满"的错误，是真正的错误
                LOG_SYSERR << "TcpConnection::sendInLoop";
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    faultError = true;  // 对端已关闭，放弃后续处理
                }
            }
            // 如果是 EWOULDBLOCK，说明 socket 发送缓冲区满，走情况 B 入队
        }
    }

    assert(remaining <= message.size());
    // 情况 B：有剩余数据需要入队
    if (!faultError && remaining > 0)
    {
        size_t oldLen = outputBuffer_.readableBytes();
        // 高水位检查：缓冲区从低于水位变为高于水位时，触发一次告警
        if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ &&
            highWaterMarkCallback_)
        {
            loop_->queueInLoop([conn = shared_from_this(),
                                cb = highWaterMarkCallback_,
                                len = oldLen + remaining]() { cb(conn, len); });
        }
        // 追加到发送缓冲区
        outputBuffer_.append(
            message.substr(static_cast<std::size_t>(nwrote), remaining));
        // 启用写事件监听 —— 当 socket 下次可写时，handleWrite 会被调用
        if (!channel_->isWriting())
        {
            channel_->enableWriting();
        }
    }
}

// ============================================================================
// handleWrite —— 续写 outputBuffer_ 到 socket
// ============================================================================

///
/// @brief 处理 socket 可写事件 —— 把 outputBuffer_ 中的数据继续写入 socket
///
/// 为什么需要 handleWrite？
///   当 sendInLoop 中的 write 没有一次写完所有数据时（socket 发送缓冲区满），
///   剩余数据存入 outputBuffer_，同时启用写事件监听。
///   当 socket 变为可写时，此方法被调用，继续尝试发送。
///
/// 注意：此方法没有显式定义在源码中，其逻辑隐含在 Channel::handleWrite
/// 回调链里。
///       如果写入完成，需要 disableWriting 以节省 poll 开销。
///

// ============================================================================
// shutdown / forceClose —— 连接关闭的跨线程安全入口
// ============================================================================

///
/// @brief 优雅关闭写端（可从任意线程调用）
///
/// 用 CAS (compare_exchange_strong) 做状态切换：
///   成功: kConnected → kDisconnecting → 提交 shutdownInLoop 到 EventLoop
///   失败: 说明已经有别的线程在关闭了，不做重复操作
///
/// 为什么需要状态机保护？
///   防止多线程同时调用 shutdown/forceClose 导致重复操作。
///   CAS 确保只有一个线程能成功执行状态转换。
///
void TcpConnection::shutdown()
{
    auto expected = StateE::kConnected;
    if (state_.compare_exchange_strong(expected, StateE::kDisconnecting,
                                       std::memory_order_release))
    {
        loop_->runInLoop([conn = shared_from_this()]
                         { conn->shutdownInLoop(); });
    }
}

///
/// @brief 在 EventLoop 线程中执行半关闭
///
/// 调用 socket_->shutdownWrite() 发送 FIN，告诉对端"我不再发送数据了"。
/// 如果此时还在等待写事件（有数据没发完），则不执行 shutdown，
/// 等 handleWrite 发完所有数据后再处理。
///
void TcpConnection::shutdownInLoop()
{
    loop_->assertInLoopThread();
    if (!channel_->isWriting())
    {
        // 只有不在等待写事件时才安全关闭写端
        socket_->shutdownWrite();
    }
}

///
/// @brief 强制关闭连接（可从任意线程调用）
///
/// 使用 CAS 循环 (compare_exchange_weak)：
///   weak 版本在 spurious failure 时重试，在高竞争场景下效率更高。
///
void TcpConnection::forceClose()
{
    auto expected = state_.load(std::memory_order_acquire);
    while (expected == StateE::kConnected || expected == StateE::kDisconnecting)
    {
        if (state_.compare_exchange_weak(expected, StateE::kDisconnecting,
                                         std::memory_order_release))
        {
            loop_->queueInLoop([conn = shared_from_this()]
                               { conn->forceCloseInLoop(); });
            break;
        }
    }
}

///
/// @brief 延迟强制关闭
///
/// 典型使用场景：
///   在消息回调中某个消息触发了一个业务逻辑导致想要关闭连接，
///   但如果直接 forceClose，可能 outputBuffer_ 中还有数据没发完。
///   用 forceCloseWithDelay(0.1) 给数据 0.1 秒"最后的发送机会"。
///
/// 内部用 weak_ptr 持有连接：
///   如果连接在延迟期间已经因其他原因被销毁，weak_ptr.lock() 返回空，
///   lambda 直接返回，不会访问悬空指针。
///
void TcpConnection::forceCloseWithDelay(double seconds)
{
    auto expected = state_.load(std::memory_order_acquire);
    while (expected == StateE::kConnected || expected == StateE::kDisconnecting)
    {
        if (state_.compare_exchange_weak(expected, StateE::kDisconnecting,
                                         std::memory_order_release))
        {
            // 使用 weak_ptr 而非 shared_ptr：
            // 避免因这个定时器持有 shared_ptr 而阻止连接正常析构
            loop_->runAfter(
                seconds,
                [weakConn = std::weak_ptr<TcpConnection>(shared_from_this())]()
                {
                    if (auto conn = weakConn.lock())
                    {
                        conn->forceClose();
                    }
                });
            break;
        }
    }
}

///
/// @brief 在 EventLoop 线程中执行强制关闭
///
void TcpConnection::forceCloseInLoop()
{
    loop_->assertInLoopThread();
    if (state_ == StateE::kConnected || state_ == StateE::kDisconnecting
        // || state_ == StateE::kDisconnected
    )
    {
        handleClose();
    }
}

// ============================================================================
// 状态查询
// ============================================================================

///
/// @brief 返回状态字符串（调试/日志用）
///
std::string_view TcpConnection::stateToString() const noexcept
{
    switch (state_)
    {
        using enum StateE;
        case kDisconnected:
            return "kDisconnected";
        case kConnecting:
            return "kConnecting";
        case kConnected:
            return "kConnected";
        case kDisconnecting:
            return "kDisconnecting";
        default:
            return "unknown state";
    }
}

// ============================================================================
// Socket 选项
// ============================================================================

void TcpConnection::setTcpNoDelay(bool on)
{
    socket_->setTcpNoDelay(on);
}

// ============================================================================
// 读写控制（跨线程安全）
// ============================================================================

///
/// @brief 启动/恢复读取
///
/// 调用 runInLoop（而非 runInLoop）确保在 EventLoop 线程执行。
///
void TcpConnection::startRead()
{
    loop_->runInLoop([conn = shared_from_this()]
                     { return conn->startReadInLoop(); });
}

///
/// @brief 在 EventLoop 线程中启用读事件监听
///
void TcpConnection::startReadInLoop()
{
    loop_->assertInLoopThread();
    if (!reading_ || !channel_->isReading())
    {
        channel_->enableReading();  // 告诉 Poller 监听 POLLIN
        reading_ = true;
    }
}

///
/// @brief 暂停读取
///
/// 暂停后，socket 接收缓冲区会逐渐填满，内核的 TCP 流控会通知对端减小发送窗口。
/// 这是一种"背压"(back-pressure) 机制。
///
void TcpConnection::stopRead()
{
    loop_->runInLoop([conn = shared_from_this()]
                     { return conn->stopReadInLoop(); });
}

///
/// @brief 在 EventLoop 线程中禁用读事件监听
///
void TcpConnection::stopReadInLoop()
{
    loop_->assertInLoopThread();
    if (reading_ || channel_->isReading())
    {
        channel_->disableReading();  // 从 Poller 中移除 POLLIN
        reading_ = false;
    }
}

// ============================================================================
// connectEstablished / connectDestroyed —— 生命周期管理（由 TcpServer 调用）
// ============================================================================

///
/// @brief 确认连接建立 —— 由 TcpServer 在 EventLoop 线程中调用
///
/// 调用时机：TcpServer accept 新连接后，创建 TcpConnection，然后在 IO 线程中
///           调用 conn->connectEstablished()。
///
/// 这个方法做了四件关键的事：
///
///   1. state_ → kConnected
///      状态从 kConnecting 变为 kConnected，允许 send() 开始工作。
///
///   2. channel_->tie(shared_from_this())
///      这是 muduo 的一个精巧设计：
///      Channel 内部持有一个 weak_ptr<void> (tie_)。
///      当 Channel 的回调被调用时，先检查 tie_ 是否还 lockable：
///        - 如果 lock 成功 → 说明 TcpConnection 对象还活着 → 继续执行回调
///        - 如果 lock 失败 → 说明 TcpConnection 已被销毁 → 跳过回调
///      这防止了"Channel 回调时连接对象已被销毁"的 use-after-free。
///
///   3. channel_->enableReading()
///      告诉 Poller 开始监听此 fd 的 POLLIN 事件。
///      这是"开闸放水"——从此开始接收数据。
///
///   4. connectionCallback_(shared_from_this())
///      通知用户"连接已建立"（通常用户在此设置高层回调）。
///
void TcpConnection::connectEstablished()
{
    loop_->assertInLoopThread();
    assert(state_ == StateE::kConnecting);
    setState(StateE::kConnected);
    // tie: Channel 回调执行前先 lock weak_ptr，确保 TcpConnection 存活
    channel_->tie(shared_from_this());
    // 开闸放水：开始监听读事件
    channel_->enableReading();

    if (connectionCallback_)
    {
        connectionCallback_(shared_from_this());
    }
}

///
/// @brief 连接销毁 —— 由 TcpServer 在 EventLoop 线程中调用
///
/// 调用时机：TcpServer 从其 connections_ map 中移除该连接后，
///           在 EventLoop 线程中调用 conn->connectDestroyed()。
///
/// 这个方法做了：
///   1. state_ → kDisconnected（如果当前还是 kConnected）
///   2. channel_->disableAll() → 停止监听所有事件
///   3. channel_->remove() → 从 Poller 的 pollfds_ 和 channels_ 中移除
///      （这是析构 assert 的关键前提！）
///   4. 调用 connectionCallback_ 通知用户"连接已断开"
///
void TcpConnection::connectDestroyed()
{
    loop_->assertInLoopThread();
    if (state_ == StateE::kConnected)
    {
        setState(StateE::kDisconnected);
        channel_->disableAll();
        if (connectionCallback_)
        {
            connectionCallback_(shared_from_this());
        }
        channel_->remove();
    }
    else if (state_ == StateE::kConnecting)
    {
        // 从未进入 kConnected 状态，Channel 未启用读事件，
        // 直接切到 kDisconnected 即可，无需操作 Poller
        setState(StateE::kDisconnected);
    }
    else
    {
        // kDisconnecting 或 kDisconnected
        if (state_ != StateE::kDisconnected)
        {
            setState(StateE::kDisconnected);
        }
        channel_->disableAll();
        channel_->remove();
    }
}

// ============================================================================
// handleClose —— 连接关闭的统一处理入口
// ============================================================================

///
/// @brief 处理连接关闭
///
/// 触发场景：
///   1. handleRead 中 read 返回 0（对端发送 FIN）
///   2. sendInLoop 中 write 返回 EPIPE 或 ECONNRESET
///   3. forceClose / forceCloseInLoop 调用
///
/// 执行步骤：
///   1. 断言当前状态合法（kConnected 或 kDisconnecting）
///   2. state_ → kDisconnected
///   3. channel_->disableAll() —— 停止监听所有事件
///   4. connectionCallback_(guardThis) —— 通知用户"连接断开了"
///   5. closeCallback_(guardThis) —— 通知 TcpServer 来清理此连接
///      └─ TcpServer 收到此回调后会把连接从 connections_ map 中移除
///
/// 为什么用 guardThis？
///   shared_from_this() 生成一个临时的 shared_ptr，延长生命周期。
///   防止 connectionCallback_ 中用户 drop 了最后一个 shared_ptr 导致
///   连接在 closeCallback_ 调用前就被析构。
///
void TcpConnection::handleClose() noexcept
{
    loop_->assertInLoopThread();
    LOG_TRACE << std::format("fd={}, state={}", channel_->fd(), stateToString());
    assert(state_ == StateE::kConnected || state_ == StateE::kDisconnecting);
    // we don't close fd, leave it to dtor, so we can find leaks easily.
    setState(StateE::kDisconnected);
    channel_->disableAll();

    // guardThis: 确保在回调执行期间连接对象不被销毁
    TcpConnectionPtr guardThis{shared_from_this()};
    if (connectionCallback_)
    {
        connectionCallback_(guardThis);
    }
    // must be the last line —— closeCallback_ 会触发 TcpServer 清理此连接
    if (closeCallback_)
    {
        closeCallback_(guardThis);
    }
}

// ============================================================================
// handleError —— 处理 socket 错误
// ============================================================================

///
/// @brief 处理 socket 错误
///
/// 通过 getsockopt(SO_ERROR) 获取 pending 的 socket 错误并记录日志。
/// SO_ERROR 被读取后会被内核清除。
///
void TcpConnection::handleError() noexcept
{
    int err = sockets::getSocketError(channel_->fd());
    LOG_ERROR << std::format(
        "TcpConnection::handleError [{}] - SO_ERROR={} {}", name_, err,
        std::error_code(err, std::system_category()).message());
}

}  // namespace chaoxi::net
