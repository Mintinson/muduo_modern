#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThreadPool.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpClient.hpp"

#include <atomic>
#include <memory>

#include <stdio.h>

using namespace chaoxi;
using namespace chaoxi::net;

class Client;

class Session
{
public:
    Session(EventLoop* loop,
            const InetAddress& serverAddr,
            const std::string& name,
            Client* owner)
        : client_(loop, serverAddr, name)
        , owner_(owner)
        , bytesRead_(0)
        , bytesWritten_(0)
        , messagesRead_(0)
    {
        client_.setConnectionCallback([this](const TcpConnectionPtr& conn)
                                      { this->onConnection(conn); });
        client_.setMessageCallback(
            [this](const TcpConnectionPtr& conn, Buffer& buf, Timestamp ts)
            { this->onMessage(conn, buf, ts); });
    }

    void start() { client_.connect(); }

    void stop() { client_.disconnect(); }

    int64_t bytesRead() const { return bytesRead_; }

    int64_t messagesRead() const { return messagesRead_; }

private:
    void onConnection(const TcpConnectionPtr& conn);

    void onMessage(const TcpConnectionPtr& conn, Buffer& buf, Timestamp)
    {
        ++messagesRead_;
        bytesRead_ += buf.readableBytes();
        bytesWritten_ += buf.readableBytes();
        conn->send(std::move(buf));
    }

    TcpClient client_;
    Client* owner_;
    int64_t bytesRead_;
    int64_t bytesWritten_;
    int64_t messagesRead_;
};

class Client
{
public:
    Client(EventLoop* loop,
           const InetAddress& serverAddr,
           int blockSize,
           int sessionCount,
           int timeout,
           int threadCount)
        : loop_(loop)
        , threadPool_(loop, "pingpong-client")
        , sessionCount_(sessionCount)
        , timeout_(timeout)
    {
        loop->runAfter(timeout, std::bind(&Client::handleTimeout, this));
        if (threadCount > 1)
        {
            threadPool_.setThreadNum(threadCount);
        }
        threadPool_.start();

        for (int i = 0; i < blockSize; ++i)
        {
            message_.push_back(static_cast<char>(i % 128));
        }

        for (int i = 0; i < sessionCount; ++i)
        {
            char buf[32];
            snprintf(buf, sizeof buf, "C%05d", i);
            // Session* session =
            //     new Session(threadPool_.getNextLoop(), serverAddr, buf, this);
            // session->start();
            sessions_.emplace_back(std::make_unique<Session>(
                threadPool_.getNextLoop(), serverAddr, buf, this));
            sessions_.back()->start();
        }
    }

    const std::string& message() const { return message_; }

    void onConnect()
    {
        // if (numConnected_.incrementAndGet() == sessionCount_)
        auto x = numConnected_.fetch_add(1
                                         // , std::memory_order_relaxed
        );

        if (x + 1 == sessionCount_)
        {
            LOG_WARN << "all connected";
        }
    }

    void onDisconnect(const TcpConnectionPtr& conn)
    {
        auto x = numConnected_.fetch_add(-1
                                         // , std::memory_order_relaxed
        );
        if (x - 1 == 0)
        {
            LOG_WARN << "all disconnected";

            int64_t totalBytesRead = 0;
            int64_t totalMessagesRead = 0;
            for (const auto& session : sessions_)
            {
                totalBytesRead += session->bytesRead();
                totalMessagesRead += session->messagesRead();
            }
            LOG_WARN << totalBytesRead << " total bytes read";
            LOG_WARN << totalMessagesRead << " total messages read";
            LOG_WARN << static_cast<double>(totalBytesRead) /
                            static_cast<double>(totalMessagesRead)
                     << " average message size";
            LOG_WARN << static_cast<double>(totalBytesRead) /
                            (timeout_ * 1024 * 1024)
                     << " MiB/s throughput";
            conn->getLoop()->queueInLoop([this] { quit(); });
        }
    }

private:
    void quit() { loop_->queueInLoop([this] { loop_->quit(); }); }

    void handleTimeout()
    {
        LOG_WARN << "stop";
        for (auto& session : sessions_)
        {
            session->stop();
        }
    }

    EventLoop* loop_;
    EventLoopThreadPool threadPool_;
    int sessionCount_;
    int timeout_;
    std::vector<std::unique_ptr<Session>> sessions_;
    std::string message_;
    std::atomic<std::int32_t> numConnected_;
};

void Session::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        conn->setTcpNoDelay(true);
        conn->send(owner_->message());
        owner_->onConnect();
    }
    else
    {
        owner_->onDisconnect(conn);
    }
}

int main(int argc, char* argv[])
{
    if (argc != 7)
    {
        fprintf(stderr, "Usage: client <host_ip> <port> <threads> <blocksize> ");
        fprintf(stderr, "<sessions> <time>\n");
    }
    else
    {
        LOG_INFO << "pid = " << process_info::pid() << ", tid = " << CurrentThread::tid();
        Logger::setLogLevel(Logger::WARN);

        const char* ip = argv[1];
        uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
        int threadCount = atoi(argv[3]);
        int blockSize = atoi(argv[4]);
        int sessionCount = atoi(argv[5]);
        int timeout = atoi(argv[6]);

        EventLoop loop;
        InetAddress serverAddr(ip, port);

        Client client(&loop, serverAddr, blockSize, sessionCount, timeout,
                      threadCount);
        loop.loop();
    }
}
