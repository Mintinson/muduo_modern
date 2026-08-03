// 使用 多线程 TcpServer，并用 mutex 来保护共享数据
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <cstdint>
#include <mutex>
#include <print>
#include <unordered_set>


#include "codec.hpp"

class ThreadedChatServer
{
public:
    ThreadedChatServer(chaoxi::net::EventLoop* loop,
                       const chaoxi::net::InetAddress& listenAddr)
        : server_(loop, listenAddr, "ThreadedChatServer")
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

    void setThreadNum(int numThreads) { server_.setThreadNum(numThreads); }

    void start() { server_.start(); }

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
    {
        LOG_INFO << conn->peerAddress().toIpPort() << " -> "
                 << conn->localAddress().toIpPort() << " is "
                 << (conn->connected() ? "UP" : "DOWN");

        std::scoped_lock lock(mutex_);
        if (conn->connected())
        {
            connections_.insert(conn);
        }
        else
        {
            connections_.erase(conn);
        }
    }

    void onStringMessage(const chaoxi::net::TcpConnectionPtr&,
                         const std::string& message,
                         chaoxi::Timestamp)
    {
        std::scoped_lock lock(mutex_);
        for (const auto& connection : connections_)
        {
            codec_.send(connection.get(), message);
        }
    }

    using ConnectionList = std::unordered_set<chaoxi::net::TcpConnectionPtr>;
    chaoxi::net::TcpServer server_;
    LengthHeaderCodec codec_;
    ConnectionList connections_;
    std::mutex mutex_;
};

int main(int argc, char* argv[])
{
  LOG_INFO << "pid = " << chaoxi::process_info::pid();
  if (argc > 1)
  {
    chaoxi::net::EventLoop loop;
    auto port = static_cast<uint16_t>(atoi(argv[1]));
    chaoxi::net::InetAddress serverAddr(port);
    ThreadedChatServer server(&loop, serverAddr);
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

