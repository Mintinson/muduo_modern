#pragma once

///
/// @file HttpServer.hpp
/// @brief 简易 HTTP 服务器 —— 基于 TcpServer + HttpContext + HttpResponse
///
/// 每条 TCP 连接持有一个 HttpContext 用于状态机解析请求。
/// 用户通过 setHttpCallback 设置路由处理函数。
///

#include "chaoxi/net/TcpServer.hpp"

namespace chaoxi::net
{

class HttpRequest;
class HttpResponse;

class HttpServer
{
public:
    using HttpCallback = std::function<void(const HttpRequest&, HttpResponse*)>;

    /// @param loop       acceptor 所在 EventLoop
    /// @param listenAddr 监听地址
    /// @param name       TcpServer 名称
    HttpServer(EventLoop* loop,
               const InetAddress& listenAddr,
               std::string name,
               TcpServer::Option option = TcpServer::Option::kNoReusePort);

    ~HttpServer() = default;

    [[nodiscard]] EventLoop* getLoop() const noexcept
    {
        return server_.getLoop();
    }

    void setHttpCallback(HttpCallback cb) noexcept
    {
        httpCallback_ = std::move(cb);
    }

    void setThreadNum(unsigned int numThreads) noexcept
    {
        server_.setThreadNum(numThreads);
    }

    void start();

private:
    /// 连接建立时：为 conn 绑定一个 HttpContext
    void onConnection(const TcpConnectionPtr& conn);

    /// 收到数据时：解析请求 → 调用用户回调 → 回复
    void onMessage(const TcpConnectionPtr& conn,
                   Buffer& buf,
                   Timestamp receiveTime);

    /// 构造 HttpResponse 并发送
    void onRequest(const TcpConnectionPtr&, const HttpRequest&);

    TcpServer server_;
    HttpCallback httpCallback_;
};

}  // namespace chaoxi::net
