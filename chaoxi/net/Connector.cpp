///
/// @file Connector.cpp
/// @brief Connector 实现 —— 非阻塞 connect + 指数退避 + 自连接检测
///

#include "chaoxi/net/Connector.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <atomic>
#include <cassert>
#include <format>
#include <memory>

namespace chaoxi::net
{

// ============================================================================
// 构造 / 析构
// ============================================================================

///
/// @brief 构造 Connector（不会立即发起连接，需调用 start()）
///
Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
    : loop_{loop}
    , serverAddr_{serverAddr}
{
    FLOG_DEBUG("ctor[{}]", (void*)this);
}

///
/// @brief 析构 —— 断言 channel_ 已经被 reset
///
/// 如果 channel_ 不为空，说明 Connector 还在 kConnecting 状态，
/// 此时 Channel 还在 Poller 中，析构会导致 use-after-free。
///
Connector::~Connector()
{
    FLOG_DEBUG("dtor[{}]", (void*)this);
    assert(!channel_);  // Channel 必须已在 stop/connect 成功时释放
}

// ============================================================================
// start / stop —— 跨线程安全的入口
// ============================================================================

///
/// @brief 开始连接（可从任意线程调用）
///
/// 通过 runInLoop 将实际逻辑编入 EventLoop 线程。
/// 使用 shared_from_this() 确保 Connector 在 lambda 执行时存活。
///
void Connector::start()
{
    connect_.store(true, std::memory_order_release);
    loop_->runInLoop([con = shared_from_this()] { con->startInLoop(); });
}

///
/// @brief 在 EventLoop 线程中执行 start 逻辑
///
/// 断言必须处于 kDisconnected 状态。
/// 如果 connect_ 为 true（没有被 stop），则开始 connect 流程。
///
void Connector::startInLoop()
{
    loop_->assertInLoopThread();
    assert(state_.load(std::memory_order_acquire) == States::kDisconnected);
    if (connect_.load(std::memory_order_acquire))
    {
        connect();  // → 非阻塞 connect + Channel 注册
    }
    else
    {
        LOG_DEBUG << "do not connect";
    }
}

///
/// @brief 停止连接（可从任意线程调用）
///
/// 通过 queueInLoop（而非 runInLoop）延迟执行，
/// 避免在回调链中立即改变状态。
///
void Connector::stop()
{
    connect_.store(false, std::memory_order_release);
    loop_->queueInLoop([con = shared_from_this()] { con->stopInLoop(); });
}

///
/// @brief 在 EventLoop 线程中执行 stop 逻辑
///
/// 如果正在连接中（kConnecting）：
///   1. 状态 → kDisconnected
///   2. 从 Poller 中移除 Channel 并获取 fd
///   3. 关闭 fd、安排可能的重试（这次 connect_ 为 false 所以 retry 会直接关闭）
///
void Connector::stopInLoop()
{
    loop_->assertInLoopThread();
    if (state_.load(std::memory_order_acquire) == States::kConnecting)
    {
        setState(States::kDisconnected);
        int sockfd = removeAndResetChannel();
        retry(sockfd);  // retry 内部检查 connect_，如果为 false 则直接 close
    }
}

// ============================================================================
// connect —— 核心：非阻塞 connect
// ============================================================================

///
/// @brief 实际执行 connect（必须在 EventLoop 线程）
///
/// 非阻塞 connect 的流程：
///
///   1. socket() → 创建一个非阻塞 socket fd
///   2. connect() → 非阻塞 connect 调用
///      - 返回 0       → 连接立即成功（罕见，通常仅在 Unix domain socket）
///      - 返回 -1:
///        - EINPROGRESS → 内核正在处理，正常流程 → 进入 connecting()
///        - EINTR       → 被信号中断，等同于 EINPROGRESS
///        - EISCONN     → 已经连接成功
///        - EAGAIN/EADDRINUSE... → 临时错误 → 进入 retry() 重试
///        - EACCES...   → 永久错误 → 关闭 fd，放弃
///
///   3. connecting() → 创建 Channel，启用写事件监听
///   4. 等待 poll 返回 fd 可写 → handleWrite() 检查 SO_ERROR
///
void Connector::connect()
{
    // ① 创建非阻塞 socket
    int sockfd = sockets::createNonblockingOrDie(serverAddr_.family());

    // ② 尝试非阻塞 connect
    int ret = sockets::connect(sockfd, serverAddr_.getSockAddr());
    int savedErrno = (ret == 0) ? 0 : errno;

    switch (savedErrno)
    {
        // ---- 成功 / 进行中 ----
        case 0:                  // 立即成功
        case EINPROGRESS:        // 正在连接（正常流程）
        case EINTR:              // 被信号中断，继续等待
        case EISCONN:            // 已经连接
            connecting(sockfd);  // → 创建 Channel 等写事件
            break;

        // ---- 临时失败 → 重试 ----
        case EAGAIN:         // 暂时无可用端口
        case EADDRINUSE:     // 本地端口被占用
        case EADDRNOTAVAIL:  // 地址不可用
        case ECONNREFUSED:   // 连接被拒绝
        case ENETUNREACH:    // 网络不可达
            retry(sockfd);   // → 关闭 fd，安排延迟重试
            break;

        // ---- 永久失败 → 放弃 ----
        case EACCES:
        case EPERM:
        case EAFNOSUPPORT:
        case EALREADY:
        case EBADF:
        case EFAULT:
        case ENOTSOCK:
            LOG_SYSERR << "connect error in Connector::startInLoop "
                       << savedErrno;
            sockets::close(sockfd);
            break;

        default:
            LOG_SYSERR << "Unexpected error in Connector::startInLoop "
                       << savedErrno;
            sockets::close(sockfd);
            break;
    }
}

// ============================================================================
// restart —— 重置并重新连接
// ============================================================================

///
/// @brief 重置状态并重新开始连接（必须在 EventLoop 线程调用）
///
/// 用于断开后的自动重连（TcpClient::removeConnection 中调用）。
/// 重试延迟被重置为初始值（500ms），因为之前的连接曾经成功过。
///
void Connector::restart()
{
    loop_->assertInLoopThread();
    setState(States::kDisconnected);
    retryDelayMs_ = kInitRetryDelayMs;  // 重置重试间隔
    connect_.store(true, std::memory_order_release);
    startInLoop();
}

// ============================================================================
// connecting —— connect 调用后的处理
// ============================================================================

///
/// @brief 创建 Channel 并启用写事件监听
///
/// connect() 之后调用（EINPROGRESS 情况）。
/// 把 socket fd 包装成 Channel，监控写事件。
/// 当 TCP 三次握手完成时，fd 变为可写，handleWrite 被调用。
///
void Connector::connecting(int sockfd)
{
    setState(States::kConnecting);
    assert(!channel_);

    // 为该 fd 创建 Channel（注册到 Poller）
    channel_ = std::make_unique<Channel>(loop_, sockfd);

    // 设置写事件回调 → handleWrite（连接完成时触发）
    channel_->setWriteCallback([con = shared_from_this()]
                               { con->handleWrite(); });

    // 设置错误回调 → handleError
    channel_->setErrorCallback([con = shared_from_this()]
                               { con->handleError(); });

    // 启用写事件监听 —— 等待内核完成 TCP 握手
    // 此时 Poller 开始监控这个 fd：当连接成功或失败时，fd 都会变为"可写"
    channel_->enableWriting();
}

// ============================================================================
// removeAndResetChannel / resetChannel
// ============================================================================

///
/// @brief 从 Poller 中移除 Channel，返回裸 fd
///
/// 调用 disableAll() + remove() 后，Channel 不再关联 Poller，
/// 然后获取裸 fd（调用者负责后续的 close）。
///
/// 注意：不在本方法内直接 reset channel_，
/// 因为在 Channel::handleEvent 的回调中不应该销毁 Channel 自身。
/// 所以通过 queueInLoop 延迟到下一个循环再 reset。
///
int Connector::removeAndResetChannel()
{
    channel_->disableAll();
    channel_->remove();
    int sockfd = channel_->fd();

    // 不能在当前回调中销毁 Channel（handleEvent 还在使用它）
    loop_->queueInLoop([con = shared_from_this()] { con->resetChannel(); });
    return sockfd;
}

///
/// @brief 析构 Channel 对象（在 queueInLoop 的回调中安全执行）
///
void Connector::resetChannel()
{
    channel_.reset();
}

// ============================================================================
// handleWrite —— 连接完成的回调
// ============================================================================

///
/// @brief 当 connect 的 fd 变为可写时被调用
///
/// 这是 Connector 最核心的方法。
/// fd 可写可能意味着三种情况：
///   1. 连接成功 —— SO_ERROR == 0 且不是自连接 → 回调 newConnectionCallback_
///   2. 连接失败 —— SO_ERROR != 0 → 重试
///   3. 自连接 —— local == peer → 重试
///
void Connector::handleWrite()
{
    auto state = state_.load(std::memory_order_acquire);
    FLOG_TRACE("Connector::handleWrite {}", stateToStr(state));

    if (state == States::kConnecting)
    {
        // 从 Poller 中移除 Channel，拿到裸 fd
        auto sockfd = removeAndResetChannel();

        // getsockopt(SO_ERROR) 获取 socket 的错误状态
        // SO_ERROR == 0 表示连接成功，非 0 表示失败
        // 另外，即便出现socket可写，也不一定意味着连接已成功建立，还需要用getsockopt(sockfd,
        // SOL_SOCKET, SO_ERROR, ...)再次确认一下。
        int err = sockets::getSocketError(sockfd);

        if (err)
        {
            // SO_ERROR != 0 → 连接失败
            std::error_code ec(err, std::system_category());
            FLOG_WARN("Connector::handleWrite - SO_ERROR = {} {}", err,
                      ec.message());
            retry(sockfd);
        }
        else if (sockets::isSelfConnect(sockfd))
        {
            // 自连接：客户端连到了自己 → 丢弃，重试
            LOG_WARN << "Connector::handleWrite - Self connect";
            retry(sockfd);
        }
        else
        {
            // 连接成功！
            setState(States::kConnected);
            if (connect_)
            {
                // 把 sockfd 交给调用者（TcpClient::newConnection）
                newConnectionCallback_(sockfd);
            }
            else
            {
                // 已经在等待期间被 stop 了 → 关闭 fd
                sockets::close(sockfd);
            }
        }
    }
    else
    {
        // 不在 kConnecting 状态却收到了写事件回调 —— 不应该发生
        assert(state == States::kDisconnected);
    }
}

// ============================================================================
// handleError —— socket 错误回调
// ============================================================================

///
/// @brief 当 connect 的 fd 发生错误时被调用
///
void Connector::handleError()
{
    auto state = state_.load(std::memory_order_acquire);
    FLOG_ERROR("Connector::handleError state={}", stateToStr(state));

    if (state == States::kConnecting)
    {
        int sockfd = removeAndResetChannel();
        int err = sockets::getSocketError(sockfd);
        std::error_code ec(err, std::system_category());
        FLOG_TRACE("SO_ERROR={} {}", err, ec.message());
        retry(sockfd);
    }
}

// ============================================================================
// retry —— 指数退避重试
// ============================================================================

///
/// @brief 关闭失败的 fd，安排延迟重试
///
/// 指数退避策略：
///   初始间隔: 500ms
///   每次失败: retryDelayMs_ = min(retryDelayMs_ * 2, 30s)
///   → 500ms → 1s → 2s → 4s → 8s → 16s → 30s → 30s → ...
///
/// 重试通过 EventLoop::runAfter 实现的：
///   创建一个定时器，在 retryDelayMs_ 毫秒后调用 startInLoop()。
///   lambda 捕获 shared_from_this()，防止 Connector 在延迟期间被析构。
///
/// @param sockfd 要关闭的 socket fd
///
void Connector::retry(int sockfd)
{
    sockets::close(sockfd);           // 关闭失败的 fd
    setState(States::kDisconnected);  // 状态 → 未连接

    if (connect_.load(std::memory_order_acquire))
    {
        FLOG_INFO("Connector::retry - Retry connecting to {} in {} ms",
                  serverAddr_.toIpPort(), retryDelayMs_);

        // 延迟重试：retryDelayMs_ 毫秒后调用 startInLoop()
        loop_->runAfter(retryDelayMs_ / 1000.0,
                        [conn = shared_from_this()] { conn->startInLoop(); });

        // 指数退避：翻倍延迟，上限 30 秒
        retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
    }
    else
    {
        // connect_ 为 false → 不再重连
        LOG_DEBUG << "do not connect";
    }
}

}  // namespace chaoxi::net
