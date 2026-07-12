// #include

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThread.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpClient.hpp"

#include <iostream>
#include <mutex>
#include <print>
#include <string>
#include <string_view>

#include <unistd.h>

#include "codec.hpp"

class ChatClient
{
public:
    ChatClient(chaoxi::net::EventLoop* loop,
               const chaoxi::net::InetAddress& serverAddr)
        : client_(loop, serverAddr, "ChatClient")
        , codec_([this](const chaoxi::net::TcpConnectionPtr& conn,
                        const std::string& message,
                        chaoxi::Timestamp timestamp)
                 { onStringMessage(conn, message, timestamp); })
    {
        client_.setConnectionCallback(
            [this](const chaoxi::net::TcpConnectionPtr& conn)
            { onConnection(conn); });
        client_.setMessageCallback(
            [this](const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf, chaoxi::Timestamp timestamp)
            { codec_.onMessage(conn, buf, timestamp); });
        client_.enableRetry();
    }

    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    void connect() { client_.connect(); }

    void disconnect() { client_.disconnect(); }

    void write(std::string_view message)
    {
        std::scoped_lock lock(mutex_);
        if (connection_)
        {
            codec_.send(connection_.get(), message);
        }
    }

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
    {
        LOG_INFO << "ChatClient - " << conn->localAddress().toIpPort() << " -> "
                 << conn->peerAddress().toIpPort() << " is "
                 << (conn->connected() ? "UP" : "DOWN");
        std::scoped_lock lock(mutex_);
        if (conn->connected())
        {
            connection_ = conn;
        }
        else
        {
            connection_.reset();
        }
    }

    void onStringMessage(const chaoxi::net::TcpConnectionPtr&,
                         const std::string& message,
                         chaoxi::Timestamp)
    {
        std::println("<<< {}", message);
    }

    chaoxi::net::TcpClient client_;
    LengthHeaderCodec codec_;
    std::mutex mutex_;
    chaoxi::net::TcpConnectionPtr connection_;
};

int main(int argc, char* argv[])
{
    LOG_INFO << "pid = " << getpid();
    if (argc > 2)
    {
        chaoxi::net::EventLoopThread loopThread;
        auto port = static_cast<uint16_t>(atoi(argv[2]));
        chaoxi::net::InetAddress serverAddr(argv[1], port);

        ChatClient client(loopThread.startLoop(), serverAddr);
        client.connect();
        std::string line;
        while (std::getline(std::cin, line))
        {
            client.write(line);
        }
        client.disconnect();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // chaoxi::net::CurrentThread::sleepUsec(1000*1000);  // wait for
        // disconnect, see ace/logging/client.cc
    }
    else
    {
        std::println("Usage: {} host_ip port", argv[0]);
    }
}
