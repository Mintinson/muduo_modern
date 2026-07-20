#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <atomic>
#include <cstdint>
#include <print>

#include <unistd.h>

int numThreads = 0;

class DiscardServer
{
public:
    DiscardServer(chaoxi::net::EventLoop* loop,
                  const chaoxi::net::InetAddress& listenAddr)
        : server_(loop, listenAddr, "DiscardServer")
        , startTime_(chaoxi::Timestamp::clock::now())
    {
        server_.setConnectionCallback(
            [this](auto&& PH1)
            { onConnection(std::forward<decltype(PH1)>(PH1)); });
        server_.setMessageCallback(
            [this](auto&& PH1, auto&& PH2, auto&& PH3)
            {
                onMessage(std::forward<decltype(PH1)>(PH1),
                          std::forward<decltype(PH2)>(PH2),
                          std::forward<decltype(PH3)>(PH3));
            });
        server_.setThreadNum(numThreads);
        loop->runEvery(3.0, [this] { printThroughput(); });
    }

    void start()
    {
        LOG_INFO << "starting " << numThreads << " threads.";
        server_.start();
    }

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
    {
        LOG_TRACE << conn->peerAddress().toIpPort() << " -> "
                  << conn->localAddress().toIpPort() << " is "
                  << (conn->connected() ? "UP" : "DOWN");
    }

    void onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf,
                   chaoxi::Timestamp)
    {
        size_t len = buf.readableBytes();
        // transferred_.add(len);
        transferred_.fetch_add(len, std::memory_order_acq_rel);
        receivedMessages_.fetch_add(1, std::memory_order_acq_rel);
        // receivedMessages_.incrementAndGet();
        buf.retrieveAll();
    }

    void printThroughput()
    {
        chaoxi::Timestamp endTime = chaoxi::Timestamp::clock::now();
        int64_t newCounter = transferred_.load(std::memory_order_acquire);
        int64_t bytes = newCounter - oldCounter_;
        int64_t msgs = receivedMessages_.load(std::memory_order_acquire);
        receivedMessages_.store(0, std::memory_order_release);
        double time = std::chrono::duration_cast<std::chrono::duration<double>>(
                          endTime - startTime_)
                          .count();
        std::println("{:4.3f} MiB/s {:4.3f} Ki Msgs/s {:6.2f} bytes per msg",
                     static_cast<double>(bytes) / time / 1024 / 1024,
                     static_cast<double>(msgs) / time / 1024,
                     static_cast<double>(bytes) / static_cast<double>(msgs));

        oldCounter_ = newCounter;
        startTime_ = endTime;
    }

    chaoxi::net::TcpServer server_;

    std::atomic<int64_t> transferred_;
    std::atomic<int64_t> receivedMessages_;
    int64_t oldCounter_{};
    chaoxi::Timestamp startTime_;
};

int main(int argc, char* argv[])
{
    LOG_INFO << "pid = " << getpid()
             << ", tid = " << chaoxi::CurrentThread::tid();
    if (argc > 1)
    {
        numThreads = atoi(argv[1]);
    }
    chaoxi::net::EventLoop loop;
    chaoxi::net::InetAddress listenAddr(2009);
    DiscardServer server(&loop, listenAddr);

    server.start();

    loop.loop();
}
