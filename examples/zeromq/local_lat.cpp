#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <print>


#include "codec.hpp"

bool g_tcpNoDelay = false;

void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        conn->setTcpNoDelay(g_tcpNoDelay);
    }
}

void onStringMessage(LengthHeaderCodec* codec,
                     const chaoxi::net::TcpConnectionPtr& conn,
                     const std::string& message,
                     chaoxi::Timestamp)
{
    codec->send(conn.get(), message);
}

int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        auto port = static_cast<std::uint16_t>(atoi(argv[1]));
        g_tcpNoDelay = (argc > 2) ? atoi(argv[2]) : true;
        int threadCount = argc > 3 ? atoi(argv[3]) : 0;

        LOG_INFO << "pid = " << chaoxi::process_info::pid()
                 << ", listen port = " << port;

        chaoxi::net::EventLoop loop;
        chaoxi::net::InetAddress listenAddr(port);
        chaoxi::net::TcpServer server{&loop, listenAddr, "PingPong"};
        LengthHeaderCodec codec{
            [capture0 = &codec](auto&& PH1, auto&& PH2, auto&& PH3)
            {
                onStringMessage(capture0, std::forward<decltype(PH1)>(PH1),
                                std::forward<decltype(PH2)>(PH2),
                                std::forward<decltype(PH3)>(PH3));
            }};
        server.setConnectionCallback(onConnection);
        server.setMessageCallback(
            [&codec](const chaoxi::net::TcpConnectionPtr& conn,
                     chaoxi::net::Buffer& buf, chaoxi::Timestamp receiveTime)
            { codec.onMessage(conn, buf, receiveTime); });

        if (threadCount > 1)
        {
            server.setThreadNum(threadCount);
        }

        server.start();
        loop.loop();
    }
    else
    {
        std::println(stderr, "Usage: {} listen_port [tcp_no_dely [threads]]",
                     argv[0]);
    }
}
