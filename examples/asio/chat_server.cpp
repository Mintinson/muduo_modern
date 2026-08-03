#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <cstdint>
#include <print>
#include <unordered_set>


#include "codec.hpp"

class ChatServer
{
public:
    ChatServer(chaoxi::net::EventLoop* loop,
               const chaoxi::net::InetAddress& listenAddr)
        : server_(loop, listenAddr, "ChatServer")
        , codec_([this](const chaoxi::net::TcpConnectionPtr& conn,
                        const std::string& message,
                        chaoxi::Timestamp timestamp)
                 { onStringMessage(conn, message, timestamp); })
    {
        server_.setConnectionCallback(
            [this](const chaoxi::net::TcpConnectionPtr& conn)
            { onConnection(conn); });
        server_.setMessageCallback(
            [this](const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf, chaoxi::Timestamp timestamp)
            { codec_.onMessage(conn, buf, timestamp); });
    }

    void start() { server_.start(); }

private:
    void onStringMessage(const chaoxi::net::TcpConnectionPtr&,
                         const std::string& message,
                         chaoxi::Timestamp)
    {
        for (const auto& connection : connections_)
        {
            codec_.send(connection.get(), message);
        }
    }

    void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
    {
        LOG_INFO << conn->peerAddress().toIpPort() << " -> "
                 << conn->localAddress().toIpPort() << " is "
                 << (conn->connected() ? "UP" : "DOWN");

        if (conn->connected())
        {
            connections_.insert(conn);
        }
        else
        {
            connections_.erase(conn);
        }
    }

    using ConnectionList = std::unordered_set<chaoxi::net::TcpConnectionPtr>;
    chaoxi::net::TcpServer server_;
    LengthHeaderCodec codec_;
    ConnectionList connections_;
};

int main(int argc, char* argv[])
{
    LOG_INFO << "pid = " << chaoxi::process_info::pid();
    if (argc > 1)
    {
        chaoxi::net::EventLoop loop;
        uint16_t port = static_cast<uint16_t>(std::stoi(argv[1]));
        chaoxi::net::InetAddress listenAddr(port);
        ChatServer server(&loop, listenAddr);
        server.start();
        loop.loop();
    }
    else
    {
        std::println("Usage: {} port", argv[0]);
    }
}
