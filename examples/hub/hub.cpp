#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Buffer.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <any>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <print>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "codec.hpp"

namespace pubsub
{
// 一个连接可能同时订阅多个主题。该集合会存入 TcpConnection::context，形成
// “连接 -> 主题”的反向索引；Topic::audiences_ 则是“主题 -> 连接”的正向索引。
// 两个方向同时维护，既能快速广播，也能在连接断开时快速清理全部订阅。
using ConnectionSubscription = std::unordered_set<std::string>;

/// 一个主题及其全部订阅者。
///
/// Topic 还保存最近一次发布的内容。新订阅者加入时可立即得到这个“保留消息”，
/// 这与许多真实消息系统的 retained message 概念相同。
class Topic
{
public:
    explicit Topic(std::string topic) : topic_(std::move(topic)) {}

    void add(const chaoxi::net::TcpConnectionPtr& conn)
    {
        audiences_.insert(conn);

        // min() 是“从未发布”的哨兵值。不能依赖 time_point 的默认值，因为
        // 默认值是 Unix epoch，并不等于 min()。
        if (lastPubTime_ != chaoxi::Timestamp::min())
        {
            conn->send(makeMessage());
        }
    }

    void remove(const chaoxi::net::TcpConnectionPtr& conn)
    {
        audiences_.erase(conn);
    }

    void publish(std::string content, chaoxi::Timestamp time)
    {
        // 先更新 retained message，再构造一次 wire message 供所有订阅者复用。
        // 避免在广播循环中为每个连接重复拼接字符串。
        content_ = std::move(content);
        lastPubTime_ = time;
        const std::string message = makeMessage();

        for (const auto& audience : audiences_)
        {
            audience->send(message);
        }
    }

private:
    std::string makeMessage() const
    {
        return "pub " + topic_ + "\r\n" + content_ + "\r\n";
    }

    std::string topic_;
    std::string content_;
    chaoxi::Timestamp lastPubTime_{chaoxi::Timestamp::min()};

    // shared_ptr 保证广播期间连接对象仍然存在；TcpServer 自身也持有活动连接。
    std::unordered_set<chaoxi::net::TcpConnectionPtr> audiences_;
};

/// 运行在单个 EventLoop 上的发布/订阅中心。
///
/// 所有连接回调、消息回调和定时器回调都串行运行在同一个 loop 线程中，因此
/// topics_ 和连接 context 不需要互斥锁。这正是 Reactor “线程归属”模型的核心
/// 优点：用事件循环的串行性换取简单、可推理的业务状态。
class PubSubServer
{
public:
    PubSubServer(chaoxi::net::EventLoop* loop,
                 const chaoxi::net::InetAddress& listenAddr)
        : loop_(loop)
        , server_(loop, listenAddr, "PubSubServer")
    {
        // TcpServer 负责连接生命周期和字节收发；PubSubServer 只注入业务回调。
        server_.setConnectionCallback(
            [this](auto&& PH1)
            { onConnection(std::forward<decltype(PH1)>(PH1)); });
        server_.setMessageCallback(
            [this](auto&& PH1, auto&& PH2, auto&& PH3)
            {
                onMessage(std::forward<decltype(PH1)>(PH1),
                          std::forward<decltype(PH2)>(PH2),
                          std::forward<decltype(PH3)>(PH3));
            });

        // 定时器与 socket 事件由同一个 EventLoop 调度，不会和 onMessage 并发。
        loop_->runEvery(1.0, [this] { timePublish(); });
    }

    void start() { server_.start(); }

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
    {
        if (conn->connected())
        {
            // context 是 TcpConnection 提供的类型擦除业务槽位。每条连接拥有
            // 自己的订阅集合，不需要在服务器中另建 connection-id map。
            conn->setContext(ConnectionSubscription());
        }
        else
        {
            auto* connectionSubscriptions =
                std::any_cast<ConnectionSubscription>(conn->getMutableContext());

            // 必须先把集合整体移出 context，再遍历这个独立快照。
            //
            // doUnsubscribe() 会从 context 中 erase(topic)。如果直接 range-for
            // 原集合，当前 unordered_set 节点会被释放，循环随后的 ++iterator
            // 就会访问已释放内存。这正是断开一个 sub 曾导致 hub 崩溃的根因。
            // unordered_set 的移动构造转移节点所有权，不逐个复制主题字符串。
            ConnectionSubscription subscriptions =
                std::move(*connectionSubscriptions);
            for (const auto& topic : subscriptions)
            {
                doUnsubscribe(conn, topic);
            }
        }
    }

    void onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf,
                   chaoxi::Timestamp receivedTime)
    {
        // 持续抽取完整帧，以同时覆盖 TCP 半包和粘包：
        // - kSuccess：消费了一帧，继续看 Buffer 中是否还有下一帧；
        // - kContinue：数据不足，保留 Buffer 等待下一次 read；
        // - kError：协议失去同步，关闭连接。
        ParseResult result = ParseResult::kSuccess;

        while (result == ParseResult::kSuccess)
        {
            std::string cmd;
            std::string topic;
            std::string content;
            result = parseMessage(buf, cmd, topic, content);

            if (result == ParseResult::kSuccess)
            {
                if (cmd == "pub")
                {
                    // 发布者的连接名仅用于标识来源；广播按 topic 路由。
                    doPublish(std::string(conn->name()), topic, content,
                              receivedTime);
                }
                else if (cmd == "sub")
                {
                    LOG_INFO << conn->name() << " subscribes " << topic;
                    doSubscribe(conn, topic);
                }
                else if (cmd == "unsub")
                {
                    doUnsubscribe(conn, topic);
                }
                else
                {
                    conn->shutdown();
                    result = ParseResult::kError;
                }
            }
            else if (result == ParseResult::kError)
            {
                conn->shutdown();
            }
        }
    }

    void timePublish()
    {
        // 服务器自己也是一个发布者。该心跳展示了定时器事件如何复用同一套
        // 发布路径；订阅 utc_time 即可每秒收到一次服务器时间。
        const auto now = chaoxi::Timestamp::clock::now();
        doPublish("internal", "utc_time", std::format("{}", now), now);
    }

    void doSubscribe(const chaoxi::net::TcpConnectionPtr& conn,
                     const std::string& topic)
    {
        auto* connSub =
            std::any_cast<ConnectionSubscription>(conn->getMutableContext());

        // 同时更新双向索引。unordered_set 使重复 sub 天然幂等。
        connSub->insert(topic);
        getTopic(topic).add(conn);
    }

    void doUnsubscribe(const chaoxi::net::TcpConnectionPtr& conn,
                       const std::string& topic)
    {
        LOG_INFO << conn->name() << " unsubscribes " << topic;

        // 删除顺序并不依赖 shared_ptr 的引用计数：当前函数参数 conn 本身会让
        // TcpConnection 至少存活到清理结束。
        getTopic(topic).remove(conn);

        auto* connSub =
            std::any_cast<ConnectionSubscription>(conn->getMutableContext());
        connSub->erase(topic);
    }

    void doPublish(const std::string& source,
                   const std::string& topic,
                   const std::string& content,
                   chaoxi::Timestamp time)
    {
        // source 当前用于展示“发布来自客户端或服务器内部”的概念；这个精简
        // 协议不把来源字段转发给订阅者。
        (void)source;
        getTopic(topic).publish(content, time);
    }

    Topic& getTopic(const std::string& topic)
    {
        // try_emplace 只在主题首次出现时构造 Topic，也避免“先查找、再插入”
        // 的两次哈希查找。
        auto [it, inserted] = topics_.try_emplace(topic, topic);
        (void)inserted;
        return it->second;
    }

    // loop_ 不拥有 EventLoop；main 中 EventLoop 的生命周期覆盖整个服务器。
    chaoxi::net::EventLoop* loop_;
    chaoxi::net::TcpServer server_;

    // topic 名称稳定地映射到 Topic；所有访问都发生在 loop_ 线程。
    std::unordered_map<std::string, Topic> topics_;
};

}  // namespace pubsub

int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        // 示例保持命令行解析简洁；正式程序应使用 from_chars 并检查端口范围。
        auto port = static_cast<uint16_t>(atoi(argv[1]));
        chaoxi::net::EventLoop loop;
        if (argc > 2)
        {
            // int inspectPort = atoi(argv[2]);
        }
        pubsub::PubSubServer server(&loop, chaoxi::net::InetAddress(port));
        server.start();

        // loop() 是进程的事件分发中心：阻塞等待 epoll、执行网络回调和定时器，
        // 直到有人调用 quit()。整个业务代码不需要显式 accept/read 循环。
        loop.loop();
    }
    else
    {
        std::println("Usage: {} pubsub_port [inspect_port]", argv[0]);
    }
}
