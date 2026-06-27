///
/// @file HttpServer.cpp
/// @brief HttpServer 实现 —— TcpConnection ↔ HttpContext ↔ HttpCallback 的桥梁
///

#include "chaoxi/net/http/HttpServer.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TcpServer.hpp"
#include "chaoxi/net/http/HttpContext.hpp"
#include "chaoxi/net/http/HttpRequest.hpp"
#include "chaoxi/net/http/HttpResponse.hpp"

namespace chaoxi::net
{

namespace
{

/// 默认回调：任何请求都返回 404
void defaultHttpCallback(const HttpRequest&, HttpResponse* resp)
{
    resp->setStatusCode(HttpResponse::HttpStatusCode::k404NotFound);
    resp->setStatusMessage("Not Found");
    resp->setCloseConnection(true);
}
}  // namespace

HttpServer::HttpServer(EventLoop* loop,
                       const InetAddress& listenAddr,
                       std::string name,
                       TcpServer::Option option)
    : server_(loop, listenAddr, std::move(name), option)
    , httpCallback_(defaultHttpCallback)
{
    // 委托 TcpServer 的连接/消息事件给 onConnection/onMessage
    server_.setConnectionCallback([this](const TcpConnectionPtr& conn)
                                  { onConnection(conn); });
    server_.setMessageCallback([this](const TcpConnectionPtr& conn, Buffer& buf,
                                      Timestamp t) { onMessage(conn, buf, t); });
}

void HttpServer::start()
{
    LOG_WARN << "HttpServer[" << server_.name() << "] starts listening on "
             << server_.ipPort();
    server_.start();
}

/// 连接建立：绑定 HttpContext 到 TcpConnection 的 context
void HttpServer::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        conn->setContext(HttpContext());
    }
}

/// 收到数据：状态机解析 → 完整请求到达调用 onRequest
void HttpServer::onMessage(const TcpConnectionPtr& conn,
                           Buffer& buf,
                           Timestamp receiveTime)
{
    auto* context = std::any_cast<HttpContext>(conn->getMutableContext());

    if (!context->parseRequest(buf, receiveTime))
    {
        // 解析失败 → 直接返回 400
        conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
        conn->shutdown();
    }

    if (context->gotAll())
    {
        onRequest(conn, context->request());
        context->reset();  // 重置状态机，准备解析下一条请求（Keep-Alive）
    }
}

/// 构造并发送 HttpResponse
void HttpServer::onRequest(const TcpConnectionPtr& conn, const HttpRequest& req)
{
    // HTTP/1.0 默认关闭连接；HTTP/1.1 默认 Keep-Alive
    const auto& connection = req.getHeader("Connection");
    bool close = connection == "close" ||
                 (req.getVersion() == HttpRequest::Version::kHttp10 &&
                  connection != "Keep-Alive");
    HttpResponse response(close);
    httpCallback_(req, &response);
    Buffer buf;
    response.appendToBuffer(buf);
    conn->send(std::move(buf));
    if (response.closeConnection())
    {
        conn->shutdown();
    }
}

}  // namespace chaoxi::net
