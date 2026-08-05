#include "pubsub.hpp"

#include <utility>

#include "codec.hpp"

namespace pubsub
{

PubSubClient::PubSubClient(chaoxi::net::EventLoop* loop,
                           const chaoxi::net::InetAddress& hubAddr,
                           const string& name)
    : client_(loop, hubAddr, name)
{
    // TcpClient 只认识通用的“连接事件”和“字节到达事件”。这里把它们适配成
    // 发布/订阅语义。lambda 捕获 this，因此 PubSubClient 必须活得比这些
    // 已注册回调更久；示例中的对象都包围着 EventLoop::loop() 生命周期。
    client_.setConnectionCallback(
        [this](auto&& PH1) { onConnection(std::forward<decltype(PH1)>(PH1)); });
    client_.setMessageCallback(
        [this](auto&& PH1, auto&& PH2, auto&& PH3)
        {
            onMessage(std::forward<decltype(PH1)>(PH1),
                      std::forward<decltype(PH2)>(PH2),
                      std::forward<decltype(PH3)>(PH3));
        });
}

void PubSubClient::start()
{
    // connect() 是异步操作；返回时 TCP 三次握手不一定已经完成。
    client_.connect();
}

void PubSubClient::stop()
{
    client_.disconnect();
}

bool PubSubClient::connected() const
{
    const auto connection = client_.connection();
    return connection && connection->connected();
}

bool PubSubClient::subscribe(const string& topic, SubscribeCallback cb)
{
    // 先安装回调再发送订阅命令，避免响应极快时消息到达却无人处理。
    string message = "sub " + topic + "\r\n";
    subscribeCallback_ = std::move(cb);
    return send(message);
}

void PubSubClient::unsubscribe(const string& topic)
{
    string message = "unsub " + topic + "\r\n";
    send(message);
}

bool PubSubClient::publish(const string& topic, const string& content)
{
    string message = "pub " + topic + "\r\n" + content + "\r\n";
    return send(message);
}

void PubSubClient::onConnection(const chaoxi::net::TcpConnectionPtr& conn)
{
    // conn 指明是哪条 TcpConnection 发生了状态变化。这个封装不把底层连接
    // 暴露给业务回调，而是让业务层通过 connected() 查询状态。
    (void)conn;

    // 教学示例没有启用自动重连，也没有保存订阅集合；生产客户端通常会在
    // 重连成功的这个回调中重放全部订阅。
    if (connectionCallback_)
    {
        connectionCallback_(this);
    }
}

void PubSubClient::onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                             chaoxi::net::Buffer& buf,
                             chaoxi::Timestamp receiveTime)
{
    // 一次 TCP read 可能包含多条完整帧，所以成功解析一条后必须继续循环；
    // 遇到 kContinue 则保留半包，等待下一次 onMessage 补齐。
    ParseResult result = ParseResult::kSuccess;
    while (result == ParseResult::kSuccess)
    {
        string cmd;
        string topic;
        string content;
        result = parseMessage(buf, cmd, topic, content);
        if (result == ParseResult::kSuccess)
        {
            if (cmd == "pub" && subscribeCallback_)
            {
                subscribeCallback_(topic, content, receiveTime);
            }
        }
        else if (result == ParseResult::kError)
        {
            // 文本协议没有恢复同步点。发现非法帧后关闭连接，比猜测下一条
            // 消息从哪里开始更安全。
            conn->shutdown();
        }
    }
}

bool PubSubClient::send(const string& message)
{
    // 取得 shared_ptr 快照后，连接至少存活到本次调用结束。TcpConnection::send
    // 自身支持跨线程调用：必要时它会复制消息并投递到所属 EventLoop。
    const auto connection = client_.connection();
    if (connection && connection->connected())
    {
        connection->send(message);
        return true;
    }
    return false;
}

}  // namespace pubsub
