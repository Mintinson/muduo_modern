#pragma once

///
/// @file TcpConnection.hpp
/// @brief TCP 连接对象 —— muduo 中的"连接"抽象
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║              TcpConnection —— 一次 TCP 连接的全生命周期              ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║  TcpConnection 是"一个已建立的 TCP 连接"的抽象，它管理：              ║
/// ║    1. 连接的 I/O（通过内部的 Channel + Socket）                      ║
/// ║    2. 连接的状态机（4 个状态）                                        ║
/// ║    3. 用户回调（连接/消息/写入完成/关闭/高水位）                     ║
/// ║    4. 收发缓冲区（inputBuffer_ / outputBuffer_）                     ║
/// ║                                                                      ║
/// ║  在 muduo 架构中的位置：                                              ║
/// ║                                                                      ║
/// ║  ┌─────────┐     ┌──────────────┐     ┌────────────────────┐        ║
/// ║  │ TcpServer│────▶│ TcpConnection│────▶│      Channel       │        ║
/// ║  │ TcpClient│     │              │     │  (fd + 回调封装)    │        ║
/// ║  └─────────┘     │ fd ◀─ Socket  │     └─────────┬──────────┘        ║
/// ║                   │ I/O ◀─ Buffer │               │                   ║
/// ║                   │ 回调 ◀─ 5 个   │     ┌─────────▼──────────┐        ║
/// ║                   │       std::funct│     │  EventLoop::Poller │       ║
/// ║                   └──────────────┘     └────────────────────┘        ║
/// ║                                                                      ║
/// ║  关键设计原则：                                                      ║
/// ║  ─────────────                                                       ║
/// ║  1. shared_from_this — 连接对象由 shared_ptr 管理，回调中始终持有    ║
/// ║     shared_ptr，防止连接在回调执行期间被销毁。                        ║
/// ║  2. Channel 不暴露给用户 — socket_ 和 channel_ 都是 private，        ║
/// ║     用户只能通过 send() / shutdown() / forceClose() 操作连接。        ║
/// ║  3. 非线程安全 + EventLoop 保证 — 所有 I/O 操作都在 EventLoop 线程   ║
/// ║     中执行。跨线程调用通过 runInLoop/queueInLoop 编入 loop 线程。     ║
/// ║  4. 状态机保护 — 用 atomic<StateE> 管理生命周期，防止重复关闭等。    ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Buffer.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <any>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace chaoxi::net {
class Channel;    // 前向声明：fd 的事件分发器
class EventLoop;  // 前向声明：Reactor 事件循环
class Socket;     // 前向声明：socket fd 的 RAII 封装

///
/// TcpConnection 表示"一次 TCP 连接"，它是**不可再生**的 —— 一旦连接断开，
/// 这个 TcpConnection 对象就作废了，不能复用它来代表另一个连接。
///
/// TcpConnection 不负责发起连接 —— 构造参数中的 sockfd 是**已经建立好连接**
/// 的 socket fd（由 TcpServer accept 或 TcpClient connect 产生）。
/// 因此初始状态是 kConnecting（正在建立，待用户确认启动）。
///
/// 继承 std::enable_shared_from_this<TcpConnection> 是为了在回调中安全地
/// 持有连接对象的 shared_ptr，防止在处理事件时连接被意外销毁。
///
/// @section 生命周期与状态机
///
///   状态转换图：
///
///     accept/connect 成功
///          │
///          ▼
///     ┌───────────┐
///     │ kConnecting│  ← 构造时的初始状态（TCP 已建立，但 Channel 监听未开启）
///     └─────┬─────┘
///           │ connectEstablished()
///           │   ├─ 设置 Channel 回调
///           │   ├─ channel_->enableReading()
///           │   └─ 调用 connectionCallback_
///           ▼
///     ┌───────────┐
///     │ kConnected │  ← 正常工作状态：可以收发数据
///     └─────┬─────┘
///           │ shutdown() / forceClose() / 对端关闭 / 读零
///           │
///           ▼
///     ┌──────────────┐
///     │ kDisconnecting│ ← 正在断开（半关闭状态），防止重入
///     └──────┬───────┘
///            │ handleClose() / connectDestroyed()
///            │   ├─ channel_->disableAll()
///            │   └─ 调用 connectionCallback_ + closeCallback_
///            ▼
///     ┌──────────────┐
///     │ kDisconnected │  ← 最终状态，析构时必须是此状态
///     └──────────────┘
///
/// @section 线程安全模型
///
///   - 所有 I/O 操作（handleRead/handleWrite/handleClose）在 EventLoop 线程执行
///   - 用户可以从任意线程调用 send() / shutdown() / forceClose()
///     └─→ 内部通过 runInLoop/queueInLoop 编入 EventLoop 线程
///   - 状态 state_ 用 std::atomic 保证跨线程可见性
///   - 用户回调（messageCallback_ 等）在 EventLoop 线程中被调用
///
/// @section 缓冲区设计
///
///   inputBuffer_  (读缓冲) : socket → Buffer → 用户回调 (messageCallback)
///   outputBuffer_ (写缓冲) : 用户 send → Buffer → socket
///
///   - 当 socket 直接可写且 outputBuffer_ 为空时，send 直接 write，不经过 Buffer
///   - 如果 write 没有一次写完（EAGAIN），剩余数据存入 outputBuffer_
///   - 启用 channel 的写事件监听，当 socket 下次可写时自动继续发送
///
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    ///
    /// @brief 构造函数 —— 包装一个已建立连接的 TCP socket
    ///
    /// @param loop      所属的 EventLoop（所有 I/O 操作都在此 loop 线程执行）
    /// @param name      连接名称（如 "server-0.0.0.0:9981#1"），用于日志和 map
    /// key
    /// @param sockfd    已建立连接的 socket 文件描述符（所有权转移给 Socket）
    /// @param localAddr 本地地址（IP + port）
    /// @param peerAddr  对端地址（IP + port）
    ///
    /// 初始状态: state_ = kConnecting
    ///
    /// 构造时做的事：
    ///   1. socket_  = 包装 sockfd 为 RAII 对象
    ///   2. channel_ = 创建 Channel 并注册到 EventLoop（但尚未启用读事件）
    ///   3. 设置 Channel
    ///   的四个内部回调（handleRead/handleWrite/handleClose/handleError）
    ///   4. 开启 TCP KeepAlive
    ///
    TcpConnection(EventLoop* loop,
                  std::string name,
                  int sockfd,
                  const InetAddress& localAddr,
                  const InetAddress& peerAddr);

    ///
    /// @brief 析构函数 —— 断言连接已处于 kDisconnected 状态
    ///
    /// 为什么必须有这个断言？
    ///   如果析构时连接还在 EventLoop 的 Poller 中（Channel 未被 remove），
    ///   析构后 Poller 会持有悬空指针 → use-after-free。
    ///   所以要求用户在析构前必须先调用 connectDestroyed()，它会调用
    ///   channel_->remove()。
    ///
    ~TcpConnection();

    // ---- 访问器 (Accessors) ----

    /// 返回所属的 EventLoop 指针
    [[nodiscard]] EventLoop* getLoop() const noexcept { return loop_; }

    /// 返回连接名称（用于日志标识）
    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    /// 返回本地地址（本机的 IP:port）
    [[nodiscard]] const InetAddress& localAddress() const noexcept {
        return localAddr_;
    }

    /// 返回对端地址（客户端的 IP:port）
    [[nodiscard]] const InetAddress& peerAddress() const noexcept {
        return peerAddr_;
    }

    /// 当前是否处于已连接状态
    [[nodiscard]] bool connected() const noexcept {
        return state_ == StateE::kConnected;
    }

    /// 当前是否已断开
    [[nodiscard]] bool disconnected() const noexcept {
        return state_ == StateE::kDisconnected;
    }

    // ---- TCP 信息 (诊断用) ----

    /// 获取 tcp_info 结构体（来自 getsockopt TCP_INFO）
    /// @return 成功返回 true
    [[nodiscard]] bool getTcpInfo(tcp_info*) const noexcept;

    /// 获取 tcp_info 的字符串表示（如未重传数、RTT 等）
    [[nodiscard]] std::string getTcpInfoString() const noexcept;

    // ---- 发送操作 (Thread Safe) ----

    ///
    /// @brief 发送字符串数据（可从任意线程调用）
    ///
    /// 如果连接已断开 (state_ != kConnected)，直接返回，不发送。
    ///
    /// @section 发送流程
    ///
    ///   用户线程调用 send(message):
    ///     └─→ 如果在 EventLoop 线程 → 直接调用 sendInLoop(message)
    ///     └─→ 如果不在 EventLoop 线程 → runInLoop(lambda)
    ///           └─→ 用 shared_from_this() 持有连接 → 确保 lambda 执行时连接存活
    ///           └─→ std::string(message) 拷贝 message → 确保数据在 lambda
    ///           中有效
    ///
    ///   sendInLoop() 内部:
    ///     1. 如果 outputBuffer_ 为空且没有在等待写事件 → 尝试直接 write
    ///     2. 如果 write 没写完 → 剩余数据 append 到 outputBuffer_
    ///     3. 开启 channel 的写事件监听 → 下次 socket 可写时自动 handleWrite()
    ///     4. 如果写完且设置了 writeCompleteCallback_ → 触发回调
    ///
    void send(std::string_view message);

    /// @brief 发送二进制 span 数据
    template <typename T>
        requires(!std::same_as<std::remove_cv_t<T>, char>)
    void send(std::span<T> message) {
        return send(
            std::string_view{reinterpret_cast<const char*>(message.data()),
                             sizeof(T) * message.size()});
    }

    ///
    /// @brief 发送 Buffer 中的数据（移动语义，零拷贝）
    ///
    /// 与 send(string_view) 的区别：
    ///   - Buffer 被 std::move 传入，避免了内存分配和拷贝
    ///   - 调用后 Buffer 内容被清空（retrieveAll）
    ///
    void send(Buffer&& message);

    // ---- 连接管理 (Thread Safe) ----

    ///
    /// @brief 优雅关闭写端（半关闭，可从任意线程调用）
    ///
    /// 内部调用 ::shutdown(fd, SHUT_WR)，告诉对端"我不再发送数据了"。
    /// 但仍然可以接收数据（对端可能继续发送）。
    ///
    /// 状态转换: kConnected → kDisconnecting
    ///
    void shutdown();

    ///
    /// @brief 强制关闭连接（可从任意线程调用）
    ///
    void forceClose();

    ///
    /// @brief 延迟强制关闭连接
    ///
    /// @param seconds 延迟的秒数
    ///
    /// 用途：避免某些竞态条件，例如在消息回调中想关闭连接，
    ///       但此时可能还有数据在 outputBuffer_ 等待发送。
    ///       延迟关闭给了数据一个"最后的发送机会"。
    ///
    void forceCloseWithDelay(double seconds);

    // ---- Socket 选项 ----

    /// 设置 TCP_NODELAY（禁用 Nagle 算法）
    void setTcpNoDelay(bool on);

    // ---- 读写控制 ----

    ///
    /// @brief 开始/恢复读取（可从任意线程调用）
    ///
    /// 内部通过 runInLoop 编入 EventLoop 线程执行。
    ///
    void startRead();

    ///
    /// @brief 暂停读取（可从任意线程调用）
    ///
    /// 暂停后，socket 接收缓冲区满时 TCP 会通知对端减小发送窗口（流量控制）。
    ///
    void stopRead();

    /// 当前是否在读取状态
    [[nodiscard]] bool isReading() const noexcept { return reading_; }

    // ---- 上下文 (User Context) ----

    /// 设置用户自定义上下文（任意类型，用于绑定业务数据）
    void setContext(const std::any& context) { context_ = context; }

    /// 获取只读的用户上下文
    [[nodiscard]] const std::any& getContext() const noexcept {
        return context_;
    }

    /// 获取可修改的用户上下文
    [[nodiscard]] std::any* getMutableContext() noexcept { return &context_; }

    // ---- 回调设置 (Not Thread Safe, 须在 EventLoop 线程调用) ----

    /// 设置连接状态变化回调（连接建立/断开时调用）
    void setConnectionCallback(ConnectionCallback cb) noexcept {
        connectionCallback_ = std::move(cb);
    }

    /// 设置消息到达回调（收到数据时调用）
    void setMessageCallback(MessageCallback cb) noexcept {
        messageCallback_ = std::move(cb);
    }

    /// 设置写入完成回调（outputBuffer_ 全部发送完毕时调用）
    void setWriteCompleteCallback(WriteCompleteCallback& cb) {
        writeCompleteCallback_ = std::move(cb);
    }

    /// 设置关闭回调（连接关闭时调用，通常由 TcpServer 内部使用）
    void setCloseCallback(CloseCallback cb) noexcept {
        closeCallback_ = std::move(cb);
    }

    /// 设置高水位回调（outputBuffer_ 超过阈值时调用）
    void setHighWaterMarkCallback(HighWaterMarkCallback cb,
                                  size_t highWaterMark) noexcept {
        highWaterMarkCallback_ = std::move(cb);
        highWaterMark_ = highWaterMark;
    }

    // ---- 供 TcpServer / TcpClient 调用的生命周期方法 ----

    ///
    /// @brief 连接建立确认 —— 由 TcpServer/TcpClient 在 EventLoop 线程调用
    ///
    /// 只能调用一次。做了三件事：
    ///   1. state_ → kConnected
    ///   2. channel_->tie(shared_from_this()) — 防止 Channel 回调时连接已析构
    ///   3. channel_->enableReading() — 开始监听 socket 读事件
    ///   4. 调用用户的 connectionCallback_ — 通知用户"连接已建立"
    ///
    void connectEstablished();

    ///
    /// @brief 连接销毁 —— 由 TcpServer 在 EventLoop 线程调用
    ///
    /// 只能调用一次。做了：
    ///   1. state_ → kDisconnected
    ///   2. channel_->disableAll() — 停止监听此 fd
    ///   3. channel_->remove() — 从 Poller 中移除（析构断言需要！）
    ///   4. 调用 connectionCallback_ — 通知用户"连接已断开"
    ///
    void connectDestroyed();

private:
    ///
    /// @brief 连接状态机
    ///
    /// kConnecting    — TCP 已建立，等待用户确认（connectEstablished）
    /// kConnected     — 正常工作状态，可以收发数据
    /// kDisconnecting — 正在断开中，防止重入
    /// kDisconnected  — 已断开，最终状态
    ///
    enum class StateE : std::uint8_t {
        kDisconnected,
        kConnecting,
        kConnected,
        kDisconnecting
    };

    /// 设置状态（使用 release 语义，确保状态之前的写操作对后续的读可见）
    void setState(StateE s) { state_.store(s, std::memory_order_release); }

    // ---- Channel 的四个事件回调（由 EventLoop 线程调用） ----

    /// @brief 处理 socket 可读事件
    ///
    /// 调用链: poll 返回 → Channel::handleEvent → readCallback_ →
    /// this->handleRead
    ///
    /// 内部:
    ///   1. inputBuffer_.readFd(fd, &savedErrno) 从 socket 读数据
    ///   2. 如果 n > 0: 调用 messageCallback_(conn, inputBuffer_, receiveTime)
    ///   3. 如果 n == 0: 对端关闭连接 → handleClose()
    ///   4. 如果 n < 0: 出错 → handleError()
    ///
    void handleRead(Timestamp receiveTime) noexcept;

    /// @brief 处理 socket 可写事件
    ///
    /// 只有在 outputBuffer_ 中有待发送数据时才启用写事件监听。
    /// 当 socket 变为可写时，尝试发送 outputBuffer_ 中的数据。
    /// 全部发送完毕后关闭写事件监听（节省 poll 开销），
    /// 并触发 writeCompleteCallback_。
    ///
    void handleWrite() noexcept;

    /// @brief 处理连接关闭
    ///
    /// 可能由以下原因触发：
    ///   1. handleRead 中 read 返回 0（对端关闭连接）
    ///   2. forceClose / shutdown 调用
    ///   3. 写错误（EPIPE, ECONNRESET）
    ///
    /// 依次调用 connectionCallback_ 和 closeCallback_，通知用户。
    ///
    void handleClose() noexcept;

    /// @brief 处理 socket 错误
    void handleError() noexcept;

    // ---- 内部辅助方法（必须在 EventLoop 线程调用） ----

    /// @brief 在 EventLoop 线程中执行实际的 send 操作
    void sendInLoop(std::string_view message);

    /// @brief sendInLoop 的模板版本
    template <typename T>
        requires(!std::same_as<std::remove_cv_t<T>, char>)
    void sendInLoop(std::span<T> message) {
        return sendInLoop(
            std::string_view{reinterpret_cast<const char*>(message.data()),
                             sizeof(T) * message.size()});
    }

    /// @brief 在 EventLoop 线程中执行 shutdown write
    void shutdownInLoop();

    /// @brief 在 EventLoop 线程中执行强制关闭
    void forceCloseInLoop();

    /// 返回当前状态的字符串表示（调试用）
    std::string_view stateToString() const noexcept;

    /// 在 EventLoop 线程中启用读事件
    void startReadInLoop();

    /// 在 EventLoop 线程中禁用读事件
    void stopReadInLoop();

    // ========================================================================
    // 成员变量
    // ========================================================================

    EventLoop* loop_;         ///< 所属 EventLoop（绝不空）
    const std::string name_;  ///< 连接名称（不可变）
    std::atomic<StateE> state_{StateE::kConnecting};  ///< 连接状态（跨线程原子）
    bool reading_{true};                              ///< 读开关（默认开启）

    // 以下两个对象是连接的 I/O 核心，不暴露给用户
    std::unique_ptr<Socket> socket_;    ///< socket fd 的 RAII 封装
    std::unique_ptr<Channel> channel_;  ///< fd 的事件分发器（注册到 Poller）

    const InetAddress localAddr_;  ///< 本地地址（不可变）
    const InetAddress peerAddr_;   ///< 对端地址（不可变）

    // ---- 用户回调（5 个） ----
    ConnectionCallback connectionCallback_;        ///< 连接建立/断开回调
    WriteCompleteCallback writeCompleteCallback_;  ///< 低水位/发送完成回调
    HighWaterMarkCallback highWaterMarkCallback_;  ///< 高水位告警回调
    MessageCallback messageCallback_;              ///< 消息到达回调
    CloseCallback closeCallback_;                  ///< 连接关闭回调

    // ---- 缓冲区 ----
    size_t highWaterMark_ = 64 * 1024 * 1024;  ///< 高水位阈值 (默认 64MB)
    Buffer inputBuffer_;                       ///< 接收缓冲区 (socket → 用户)
    Buffer outputBuffer_;                      ///< 发送缓冲区 (用户 → socket)

    std::any context_;  ///< 用户自定义上下文（可绑定任意业务数据）
};

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

}  // namespace chaoxi::net
