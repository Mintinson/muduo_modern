#pragma once

///
/// @file Connector.hpp
/// @brief 非阻塞 TCP 连接器 —— 负责 connect() + 指数退避重试
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║    Connector —— 非阻塞 connect 的艺术                                ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║  Connector 负责主动发起 TCP 连接，是整个客户端栈的"发动机"。         ║
/// ║                                                                      ║
/// ║  核心思想：                                                           ║
/// ║  ─────────                                                           ║
/// ║  普通的 connect() 是阻塞的 —— 调用线程会被挂起直到连接成功或失败。    ║
/// ║  非阻塞 connect() 立即返回，然后内核在后台完成三次握手：              ║
/// ║    1. socket() + fcntl(O_NONBLOCK)                                   ║
/// ║    2. connect() → 立即返回 -1，errno = EINPROGRESS                   ║
/// ║       （表示"内核正在处理连接请求"）                                   ║
/// ║    3. 将 socket fd 包装成 Channel，启用写事件监听                     ║
/// ║    4. 当连接完成（成功或失败），内核让 fd 变为可写                     ║
/// ║    5. poll 返回 → handleWrite → getsockopt(SO_ERROR) 获取结果        ║
/// ║       - SO_ERROR == 0  → 连接成功 → 回调 newConnectionCallback       ║
/// ║       - SO_ERROR != 0  → 连接失败 → 重试                              ║
/// ║                                                                      ║
/// ║  自连接（Self-Connect）是什么？                                       ║
/// ║  ─────────────────────────────                                       ║
/// ║  当客户端 bind 的端口和它 connect 的端口相同时，内核可能让连接        ║
/// ║  "成功"但实际是连到了自己。Connector 检测这种情况并重试。             ║
/// ║                                                                      ║
/// ║  指数退避重试策略：                                                   ║
/// ║  ─────────────────                                                   ║
/// ║  初始: 500ms                                                         ║
/// ║  每次失败: 延迟翻倍 → 1s → 2s → 4s → 8s → ... → 上限 30s            ║
/// ║  成功: 重置回 500ms                                                  ║
/// ║                                                                      ║
/// ║  状态机：                                                             ║
/// ║  ────────                                                             ║
/// ║                                                                      ║
/// ║  start()                                                              ║
/// ║    │                                                                  ║
/// ║    ▼                                                                  ║
/// ║  ┌──────────────┐                                                    ║
/// ║  │ kDisconnected │ ← 初始 / 重试间隔等待中                             ║
/// ║  └──────┬───────┘                                                    ║
/// ║         │ connect()                                                   ║
/// ║         ▼                                                             ║
/// ║  ┌──────────────┐                                                    ║
/// ║  │  kConnecting  │ ← connect 已调用，等待 poll 可写                  ║
/// ║  └──────┬───────┘                                                    ║
/// ║         │ handleWrite                                                ║
/// ║         │ SO_ERROR == 0 && !selfConnect                              ║
/// ║         ▼                                                             ║
/// ║  ┌──────────────┐    连接断开 / stop()                                ║
/// ║  │  kConnected   │─────────────────────────────▶ kDisconnected       ║
/// ║  └──────────────┘                                                    ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝

#include "chaoxi/net/InetAddress.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace chaoxi::net
{
class Channel;
class EventLoop;

///
/// @brief 非阻塞 TCP 连接器
///
/// 使用方式：
///   1. 构造 Connector(loop, serverAddr)
///   2. setNewConnectionCallback(cb) — 设置连接成功的回调
///   3. start() — 开始连接（会重试直到成功或 stop）
///   4. stop()  — 停止连接/重试
///
/// 继承 enable_shared_from_this 的原因：
///   重试定时器使用 weak_ptr 或 shared_from_this() 持有 Connector，
///   防止在定时器回调中 Connector 已被析构。
///
class Connector : public std::enable_shared_from_this<Connector>
{
public:
    /// 连接成功后的回调：void(int sockfd)，sockfd 的所有权转移给调用者
    using NewConnectionCallback = std::function<void(int sockfd)>;

    Connector(EventLoop* loop, const InetAddress& serverAddr);
    ~Connector();
    Connector(const Connector&) = delete;
    Connector(Connector&&) noexcept = delete;
    Connector& operator=(const Connector&) = delete;
    Connector& operator=(Connector&&) noexcept = delete;

    void setNewConnectionCallback(NewConnectionCallback cb) noexcept
    {
        newConnectionCallback_ = std::move(cb);
    }

    /// @brief 开始连接（可从任意线程调用）
    void start();

    /// @brief 重置并重新开始连接（必须在 EventLoop 线程调用）
    void restart();

    /// @brief 停止连接（可从任意线程调用）
    void stop();

    const InetAddress& serverAddress() const noexcept { return serverAddr_; }

private:
    /// 内部状态机（比 TcpConnection 简单）
    enum class States : uint8_t
    {
        kDisconnected,  // 未连接或已断开
        kConnecting,    // 正在连接（waiting for writable event）
        kConnected      // 已连接
    };

    static constexpr std::string_view stateToStr(States state) noexcept
    {
        switch (state)
        {
            case States::kDisconnected:
                return "Disconnected";
            case States::kConnecting:
                return "Connecting";
            case States::kConnected:
                return "Connected";
            default:
                return "Unknown State";
        }
    }

    static constexpr int kMaxRetryDelayMs = 30 * 1000;  ///< 最大重试间隔 30s
    static constexpr int kInitRetryDelayMs = 500;       ///< 初始重试间隔 500ms

    void setState(States s) noexcept
    {
        state_.store(s, std::memory_order_release);
    }

    // ---- 只能在 EventLoop 线程调用的方法 ----
    void startInLoop();
    void stopInLoop();
    void connect();               ///< 实际执行 socket() + 非阻塞 connect()
    void connecting(int sockfd);  ///< connect 后的处理（创建 Channel 等写事件）
    void handleWrite();           ///< connect 完成时的回调
    void handleError();           ///< socket 错误回调
    void retry(int sockfd);       ///< 关闭 sockfd，安排重试
    int removeAndResetChannel();  ///< 从 Poller 移除 Channel，返回 fd
    void resetChannel();          ///< 重置 channel_ unique_ptr

    EventLoop* loop_;
    InetAddress serverAddr_;
    std::atomic<bool> connect_{false};                  ///< 是否允许连接/重连
    std::atomic<States> state_{States::kDisconnected};  ///< 当前状态

    std::unique_ptr<Channel> channel_;             ///< 仅在 kConnecting 期间存在
    NewConnectionCallback newConnectionCallback_;  ///< 连接成功后的回调
    int retryDelayMs_{kInitRetryDelayMs};          ///< 当前重试间隔（指数增长）
};
}  // namespace chaoxi::net
