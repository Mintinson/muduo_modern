// 使用 copy-on-write 技巧来降低锁竞争

// 使用 多线程 TcpServer，并用 mutex 来保护共享数据
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/base/ThreadLocalSingleton.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <print>
#include <set>
#include <unordered_set>
#include <utility>


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

    void start()
    {
        // 设置线程初始化回调函数，在每个线程中初始化线程局部变量
        server_.setThreadInitCallback([this](chaoxi::net::EventLoop* cb)
                                      { threadInit(cb); });
        server_.start();
    }

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
    {
        LOG_INFO << conn->peerAddress().toIpPort() << " -> "
                 << conn->localAddress().toIpPort() << " is "
                 << (conn->connected() ? "UP" : "DOWN");

        if (conn->connected())
        {
            LocalConnections::instance().insert(conn);
        }
        else
        {
            LocalConnections::instance().erase(conn);
        }
    }

    void onStringMessage(const chaoxi::net::TcpConnectionPtr&,
                         const std::string& msg,
                         chaoxi::Timestamp)
    {
        LOG_DEBUG;

        std::scoped_lock lock(mutex_);
        for (auto loop : loops_)
        {
            loop->queueInLoop([this, msg]() { distributeMessage(msg); });
        }
        LOG_DEBUG;
    }

    void distributeMessage(const std::string& message)
    {
        LOG_DEBUG << "begin";
        for (const auto& it : LocalConnections::instance())
        {
            codec_.send(it.get(), message);
        }
        LOG_DEBUG << "end";
    }

    void threadInit(chaoxi::net::EventLoop* loop)
    {
        // assert(L)
        (void)LocalConnections::instance();
        std::scoped_lock lock(mutex_);
        loops_.insert(loop);
    }

    using ConnectionList = std::unordered_set<chaoxi::net::TcpConnectionPtr>;
    using LocalConnections = chaoxi::ThreadLocalSingleton<ConnectionList>;

    chaoxi::net::TcpServer server_;
    LengthHeaderCodec codec_;
    std::mutex mutex_;
    std::unordered_set<chaoxi::net::EventLoop*> loops_;
};

int main(int argc, char* argv[])
{
    LOG_INFO << "pid = " << chaoxi::process_info::pid();
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
