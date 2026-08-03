
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <any>
#include <array>
#include <cstdio>
#include <memory>
#include <print>


void onHighWaterMark(const chaoxi::net::TcpConnectionPtr& conn, size_t len)
{
    LOG_INFO << "HighWaterMark " << len;
}

const char* g_file = nullptr;
constexpr std::size_t kBuffSize = 64 * 1024;
using FilePtr = std::shared_ptr<FILE>;

void onConnection(const chaoxi::net::TcpConnectionPtr& conn)
{
    LOG_INFO << "FileServer - " << conn->peerAddress().toIpPort() << " -> "
             << conn->localAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");
    if (conn->connected())
    {
        LOG_INFO << "FileServer - Sending file " << g_file << " to "
                 << conn->peerAddress().toIpPort();
        conn->setHighWaterMarkCallback(onHighWaterMark, 64 * 1024);
        FILE* fp = ::fopen(g_file, "rb");
        if (fp)
        {
            FilePtr ctx(fp, ::fclose);
            conn->setContext(ctx);
            // char buf[kBufSize];
            std::array<char, kBuffSize> buf{};
            size_t nread = ::fread(buf.data(), 1, buf.size(), ctx.get());
            conn->send(buf);
        }
        else
        {
            conn->shutdown();
            LOG_INFO << "FileServer - no such file";
        }
    }
    // else
    // {
    //     if (conn->getContext().has_value())
    //     {
    //         FILE* fp = std::any_cast<FILE*>(conn->getContext());
    //         if (fp)
    //         {
    //             ::fclose(fp);
    //         }
    //     }
    // }
}

void onWriteComplete(const chaoxi::net::TcpConnectionPtr& conn)
{
    const auto& fp = std::any_cast<const FilePtr&>(conn->getContext());
    std::array<char, kBuffSize> buf{};

    auto nread = ::fread(buf.data(), 1, buf.size(), fp.get());
    if (nread > 0)
    {
        conn->send(buf);
    }
    else
    {
        // ::fclose(fp);
        // fp = nullptr;
        // conn->setContext(fp);
        conn->shutdown();
        LOG_INFO << "FileServer - done";
    }
}

int main(int argc, char* argv[])
{
    LOG_INFO << "pid= " << chaoxi::process_info::pid();
    if (argc > 1)
    {
        g_file = argv[1];

        chaoxi::net::EventLoop loop;
        chaoxi::net::InetAddress listenerAddr(2021);
        chaoxi::net::TcpServer server(&loop, listenerAddr, "FileTransferServer");
        server.setConnectionCallback(onConnection);
        server.setWriteCompleteCallback(onWriteComplete);
        server.start();
        loop.loop();
    }
    else
    {
        std::println(stderr, "Usage: {} <file_path>", argv[0]);
    }
}
