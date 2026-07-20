#include "chaoxi/base/CurrentThread.hpp"
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <print>

#include <chaoxi/net/TcpClient.hpp>
#include <unistd.h>

class DiscardClient
{
public:
    DiscardClient(chaoxi::net::EventLoop* loop,
                  const chaoxi::net::InetAddress& listenAddr,
                  int size)
        : loop_(loop)
        , client_(loop, listenAddr, "DiscardClient")
        , message_(size, 'H')
    {
        client_.setConnectionCallback(
            [this](auto&& PH1)
            { onConnection(std::forward<decltype(PH1)>(PH1)); });
        client_.setMessageCallback(
            [this](auto&& PH1, auto&& PH2, auto&& PH3)
            {
                onMessage(std::forward<decltype(PH1)>(PH1),
                          std::forward<decltype(PH2)>(PH2),
                          std::forward<decltype(PH3)>(PH3));
            });
        client_.setWriteCompleteCallback(
            [this](auto&& PH1)
            { onWriteComplete(std::forward<decltype(PH1)>(PH1)); });
    }

    void connect() { client_.connect(); }

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
    {
        LOG_TRACE << conn->localAddress().toIpPort() << " -> "
                  << conn->peerAddress().toIpPort() << " is "
                  << (conn->connected() ? "UP" : "DOWN");

        if (conn->connected())
        {
            conn->setTcpNoDelay(true);
            conn->send(message_);
        }
        else
        {
            loop_->quit();
        }
    }

    void onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf,
                   chaoxi::Timestamp time)
    {
        buf.retrieveAll();
    }

    void onWriteComplete(const chaoxi::net::TcpConnectionPtr& conn)
    {
        LOG_INFO << "write complete " << message_.size();
        conn->send(message_);
    }

    chaoxi::net::EventLoop* loop_;
    chaoxi::net::TcpClient client_;
    std::string message_;
};

int main(int argc, char* argv[])

{
    LOG_INFO << "pid = " << getpid()
             << ", tid = " << chaoxi::CurrentThread::tid();

    if (argc > 1)
    {
        chaoxi::net::EventLoop loop;
        chaoxi::net::InetAddress serverAddr(argv[1], 2009);
        int size = 256;
        if (argc > 2)
        {
            size = atoi(argv[2]);
        }
        DiscardClient client(&loop, serverAddr, size);
        client.connect();
        loop.loop();
    }
    else
    {
        std::println("Usage: {} host_ip", argv[0]);
    }
}