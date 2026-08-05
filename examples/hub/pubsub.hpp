#pragma once

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpClient.hpp"

#include <functional>
#include <string>

namespace pubsub
{
using std::string;

/// 发布/订阅客户端的薄封装。
///
/// PubSubClient 不创建线程：TcpClient 的连接、收包和回调都由构造时传入的
/// EventLoop 驱动。publish() 只取得 TcpClient 线程安全发布的连接快照，并
/// 调用 TcpConnection 的跨线程安全 send()，因此 stdin 发布模式可以从主线程
/// 调用它；订阅回调和连接回调仍在 EventLoop 线程执行。
///
/// 典型生命周期：
///   start() -> 连接回调 -> subscribe()/publish() -> stop() -> 断开回调
class PubSubClient
{
public:
    // 连接建立和连接断开都会调用该回调；调用者通过 connected() 区分状态。
    using ConnectionCallback = std::function<void(PubSubClient*)>;

    // 收到 pub 帧时调用。topic 参数让一个回调可以服务多个订阅主题。
    using SubscribeCallback = std::function<void(
        const std::string&, const std::string&, chaoxi::Timestamp)>;

    PubSubClient(chaoxi::net::EventLoop* loop,
                 const chaoxi::net::InetAddress& hubAddr,
                 const std::string& name);

    /// 发起异步连接；真正建立连接后才会触发 ConnectionCallback。
    void start();

    /// 发起优雅断开；真正断开后会再次触发 ConnectionCallback。
    void stop();

    /// 当前是否持有一条状态为 connected 的 TcpConnection。
    bool connected() const;

    void setConnectionCallback(ConnectionCallback cb)
    {
        connectionCallback_ = std::move(cb);
    }

    /// 发送订阅命令，并设置收到发布消息时使用的回调。
    bool subscribe(const string& topic, SubscribeCallback cb);

    /// 发送取消订阅命令。断线时 hub 也会自动清理订阅关系。
    void unsubscribe(const string& topic);

    /// 发布一条消息。返回 false 表示当前没有可用连接，消息没有发送。
    bool publish(const string& topic, const string& content);

private:
    // 以下两个函数是 TcpClient 回调入口，只在 EventLoop 线程中执行。
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn);
    void onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf,
                   chaoxi::Timestamp receiveTime);
    bool send(const string& message);

    // TcpClient 管理重连器和底层 TcpConnection 的创建。
    chaoxi::net::TcpClient client_;

    ConnectionCallback connectionCallback_;
    SubscribeCallback subscribeCallback_;
};
}  // namespace pubsub
