#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <print>


using namespace chaoxi;
using namespace chaoxi::net;

void onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        conn->setTcpNoDelay(true);
    }
}

void onMessage(const TcpConnectionPtr& conn, Buffer& buf, Timestamp)
{
    conn->send(std::move(buf));
}

int main(int argc, char* argv[])
{
    if (argc < 4)
    {
        std::println(stderr, "Usage: server <address> <port> <threads>");
    }
    else
    {
        LOG_INFO << "pid = " << process_info::pid() << ", tid = " << CurrentThread::tid();
        Logger::setLogLevel(Logger::LogLevel::WARN);

        const char* ip = argv[1];
        auto port = static_cast<uint16_t>(atoi(argv[2]));
        InetAddress listenAddr(ip, port);
        int threadCount = atoi(argv[3]);

        EventLoop loop;

        TcpServer server(&loop, listenAddr, "PingPong");

        server.setConnectionCallback(onConnection);
        server.setMessageCallback(onMessage);

        if (threadCount > 1)
        {
            server.setThreadNum(threadCount);
        }

        server.start();

        loop.loop();
    }
}
