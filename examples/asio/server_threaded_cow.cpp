// 使用 copy-on-write 技巧来降低锁竞争

// 使用 多线程 TcpServer，并用 mutex 来保护共享数据
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <print>
#include <unordered_set>

#include <unistd.h>

#include "codec.hpp"

class ThreadedCoWChatServer
{
public:
    ThreadedCoWChatServer(chaoxi::net::EventLoop* loop,
                          const chaoxi::net::InetAddress& listenAddr)
        : server_(loop, listenAddr, "ThreadedCoWChatServer")
        , codec_([this](const chaoxi::net::TcpConnectionPtr& conn,
                        const std::string& message,
                        chaoxi::Timestamp timestamp)
                 { onStringMessage(conn, message, timestamp); })
        , connections_(std::make_shared<ConnectionList>())
    {
        server_.setConnectionCallback(
            [this](const chaoxi::net::TcpConnectionPtr& conn)
            { onConnection(conn); });
        server_.setMessageCallback(
            [this](const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf, chaoxi::Timestamp timestamp)
            { codec_.onMessage(conn, buf, timestamp); });
    }

    void setThreadNum(int numThreads) { server_.setThreadNum(numThreads); }

    void start() { server_.start(); }

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
    {
        LOG_INFO << conn->peerAddress().toIpPort() << " -> "
                 << conn->localAddress().toIpPort() << " is "
                 << (conn->connected() ? "UP" : "DOWN");

        std::scoped_lock lock(mutex_);
        // 检查是否有其他线程持有该指针
        if (connections_.use_count() != 1)
        {
            // 有多个线程持有 connections_，需要复制一份新的 ConnectionList
            connections_ = std::make_shared<ConnectionList>(*connections_);
        }

        // 更新连接列表
        if (conn->connected())
        {
            connections_->insert(conn);
        }
        else
        {
            connections_->erase(conn);
        }
    }

    auto getConnectionList()
    {
        std::scoped_lock lock(mutex_);
        return connections_;
    }

    void onStringMessage(const chaoxi::net::TcpConnectionPtr&,
                         const std::string& message,
                         chaoxi::Timestamp)
    {
        // std::scoped_lock lock(mutex_); // no need lock
        auto connections = getConnectionList();  // copy-on-write
        for (const auto& connection : *connections_)
        {
            codec_.send(connection.get(), message);
        }
    }

    using ConnectionList = std::unordered_set<chaoxi::net::TcpConnectionPtr>;
    using ConnectionListPtr = std::shared_ptr<ConnectionList>;
    chaoxi::net::TcpServer server_;
    LengthHeaderCodec codec_;
    ConnectionListPtr connections_;
    std::mutex mutex_;
};

int main(int argc, char* argv[])
{
    LOG_INFO << "pid = " << getpid();
    if (argc > 1)
    {
        chaoxi::net::EventLoop loop;
        auto port = static_cast<uint16_t>(atoi(argv[1]));
        chaoxi::net::InetAddress serverAddr(port);
        ThreadedCoWChatServer server(&loop, serverAddr);
        if (argc > 2)
        {
            server.setThreadNum(atoi(argv[2]));
        }
        server.start();
        loop.loop();
    }
    else
    {
        std::println("Usage: {} port [thread_num]", argv[0]);
    }
}
