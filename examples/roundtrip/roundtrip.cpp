#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TcpClient.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <print>

constexpr std::size_t frameLen = 2 * sizeof(int64_t);

void serverConnectionCallback(const chaoxi::net::TcpConnectionPtr& conn)
{
    LOG_TRACE << conn->name() << " " << conn->peerAddress().toIpPort() << " -> "
              << conn->localAddress().toIpPort() << " is "
              << (conn->connected() ? "UP" : "DOWN");
    if (conn->connected())
    {
        conn->setTcpNoDelay(true);
    }
    else
    {
    }
}

void serverMessageCallback(const chaoxi::net::TcpConnectionPtr& conn,
                           chaoxi::net::Buffer& buffer,
                           chaoxi::Timestamp receiveTime)
{
    int64_t message[2];
    // 用 while 而不是 if 的核心原因是：一次 read 可能返回多个完整的应用层消息。
    // TCP 只保证字节流的可靠传输，不保留消息边界。假设客户端连续发送了 3
    // 个消息： 服务器端可能一次 read 就把 3 个消息都读到 buffer 里了，所以需要
    // while 循环来处理所有完整的消息。
    while (buffer.readableBytes() >= frameLen)
    {
        memcpy(message, buffer.peek(), frameLen);
        buffer.retrieve(frameLen);
        message[1] = receiveTime.time_since_epoch().count();
        conn->send({reinterpret_cast<char*>(message), sizeof message});
    }
}

void runServer(uint16_t port)
{
    chaoxi::net::EventLoop loop;
    chaoxi::net::TcpServer server(&loop, chaoxi::net::InetAddress(port),
                                  "ClockServer");
    server.setConnectionCallback(serverConnectionCallback);
    server.setMessageCallback(serverMessageCallback);
    server.start();
    loop.loop();
}

chaoxi::net::TcpConnectionPtr clientConnection;

void clientConnectionCallback(const chaoxi::net::TcpConnectionPtr& conn)
{
    LOG_TRACE << conn->localAddress().toIpPort() << " -> "
              << conn->peerAddress().toIpPort() << " is "
              << (conn->connected() ? "UP" : "DOWN");
    if (conn->connected())
    {
        clientConnection = conn;
        conn->setTcpNoDelay(true);
    }
    else
    {
        clientConnection.reset();
    }
}

void clientMessageCallback(const chaoxi::net::TcpConnectionPtr&,
                           chaoxi::net::Buffer& buffer,
                           chaoxi::Timestamp receiveTime)
{
    int64_t message[2];
    while (buffer.readableBytes() >= frameLen)
    {
        memcpy(message, buffer.peek(), frameLen);
        buffer.retrieve(frameLen);
        int64_t send = message[0];
        int64_t their = message[1];
        int64_t back = receiveTime.time_since_epoch().count();
        int64_t mine = (back + send) / 2;
        LOG_INFO << "round trip " << (back - send) / 1000.0 << "ms, clock error "
                 << (their - mine) / 1000.0 << "ms";
    }
}

void sendMyTime()
{
    if (clientConnection)
    {
        int64_t message[2] = {0, 0};
        message[0] = chaoxi::Timestamp::clock::now().time_since_epoch().count();
        clientConnection->send(
            {reinterpret_cast<char*>(message), sizeof message});
    }
}

void runClient(const char* ip, uint16_t port)
{
    chaoxi::net::EventLoop loop;
    chaoxi::net::TcpClient client(&loop, chaoxi::net::InetAddress(ip, port),
                                  "ClockClient");
    client.enableRetry();
    client.setConnectionCallback(clientConnectionCallback);
    client.setMessageCallback(clientMessageCallback);
    client.connect();
    loop.runEvery(0.2, sendMyTime);
    loop.loop();
}

int main(int argc, char* argv[])
{
    if (argc > 2)
    {
        auto port = static_cast<uint16_t>(atoi(argv[2]));
        if (strcmp(argv[1], "-s") == 0)
        {
            runServer(port);
        }
        else
        {
            runClient(argv[1], port);
        }
    }
    else
    {
        std::println("Usage:\n{} -s port\n{} ip port", argv[0], argv[0]);
    }
}
