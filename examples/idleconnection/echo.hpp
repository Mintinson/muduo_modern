
#pragma once

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpConnection.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <memory>
#include <print>
#include <unordered_set>

#include <assert.h>
#include <stdio.h>

using namespace chaoxi;
using namespace chaoxi::net;

// RFC 862
class EchoServer
{
public:
    EchoServer(chaoxi::net::EventLoop* loop,
               const chaoxi::net::InetAddress& listenAddr,
               int idleSeconds);

    void start();

private:
    void onConnection(const chaoxi::net::TcpConnectionPtr& conn);

    void onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf,
                   chaoxi::Timestamp time);

    void onTimer();

    void dumpConnectionBuckets() const;

    using WeakTcpConnectionPtr = std::weak_ptr<chaoxi::net::TcpConnection>;

    struct Entry
    {
        explicit Entry(const WeakTcpConnectionPtr& weakConn)
            : weakConn_(weakConn)
        {
        }

        ~Entry()
        {
            auto conn = weakConn_.lock();
            if (conn)
            {
                conn->shutdown();
            }
        }

        WeakTcpConnectionPtr weakConn_;
    };

    using EntryPtr = std::shared_ptr<Entry>;
    using WeakEntryPtr = std::weak_ptr<Entry>;
    using Bucket = std::unordered_set<EntryPtr>;
    using WeakConnectionList = boost::circular_buffer<Bucket>;

    chaoxi::net::TcpServer server_;
    WeakConnectionList connectionBuckets_;
};

EchoServer::EchoServer(EventLoop* loop,
                       const InetAddress& listenAddr,
                       int idleSeconds)
    : server_(loop, listenAddr, "EchoServer")
    , connectionBuckets_(idleSeconds)
{
    server_.setConnectionCallback(
        [this](auto&& PH1) { onConnection(std::forward<decltype(PH1)>(PH1)); });
    server_.setMessageCallback(
        [this](auto&& PH1, auto&& PH2, auto&& PH3)
        {
            onMessage(std::forward<decltype(PH1)>(PH1),
                      std::forward<decltype(PH2)>(PH2),
                      std::forward<decltype(PH3)>(PH3));
        });
    loop->runEvery(1.0, [this] { onTimer(); });
    connectionBuckets_.resize(idleSeconds);
    dumpConnectionBuckets();
}

void EchoServer::start()
{
    server_.start();
}

void EchoServer::onConnection(const TcpConnectionPtr& conn)
{
    LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort() << " -> "
             << conn->localAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");

    if (conn->connected())
    {
        EntryPtr entry(new Entry(conn));
        connectionBuckets_.back().insert(entry);
        dumpConnectionBuckets();
        WeakEntryPtr weakEntry(entry);
        conn->setContext(weakEntry);
    }
    else
    {
        assert(!conn->getContext().empty());
        auto weakEntry(std::any_cast<WeakEntryPtr>(conn->getContext()));
        LOG_DEBUG << "Entry use_count = " << weakEntry.use_count();
    }
}

void EchoServer::onMessage(const TcpConnectionPtr& conn,
                           Buffer& buf,
                           Timestamp time)
{
    std::string msg(buf.retrieveAllAsString());
    LOG_INFO << conn->name() << " echo " << msg.size() << " bytes at " << time;
    conn->send(msg);

    assert(!conn->getContext().empty());
    auto weakEntry(std::any_cast<WeakEntryPtr>(conn->getContext()));
    EntryPtr entry(weakEntry.lock());
    if (entry)
    {
        connectionBuckets_.back().insert(entry);
        dumpConnectionBuckets();
    }
}

void EchoServer::onTimer()
{
    connectionBuckets_.push_back(Bucket());
    dumpConnectionBuckets();
}

void EchoServer::dumpConnectionBuckets() const
{
    LOG_INFO << "size = " << connectionBuckets_.size();
    int idx = 0;
    for (WeakConnectionList::const_iterator bucketI = connectionBuckets_.begin();
         bucketI != connectionBuckets_.end(); ++bucketI, ++idx)
    {
        const Bucket& bucket = *bucketI;
        std::print("[{}] len = {} : ", idx, bucket.size());
        for (const auto& it : bucket)
        {
            bool connectionDead = it->weakConn_.expired();
            printf("%p(%ld)%s, ", get_pointer(it), it.use_count(),
                   connectionDead ? " DEAD" : "");
        }
        puts("");
    }
}
