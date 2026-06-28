
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <cstdio>
#include <print>

#include <unistd.h>

const char* g_file = nullptr;
// FIXME: use FileUtil::readFile()
std::string readFile(const char* filename)
{
  std::string content;
  FILE* fp = ::fopen(filename, "rb");
  if (fp)
  {
    // inefficient!!!
    const int kBufSize = 1024*1024;
    char iobuf[kBufSize];
    ::setbuffer(fp, iobuf, sizeof iobuf);

    char buf[kBufSize];
    size_t nread = 0;
    while ( (nread = ::fread(buf, 1, sizeof buf, fp)) > 0)
    {
      content.append(buf, nread);
    }
    ::fclose(fp);
  }
  return content;
}

void onHighWaterMark(const chaoxi::net::TcpConnectionPtr& conn, size_t len)
{
    LOG_INFO << "HighWaterMark " << len;
}

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
        auto fileContent = readFile(g_file);
        conn->send(fileContent);
        conn->shutdown();
        LOG_INFO << "FileServer - done";
    }
}

int main(int argc, char* argv[])
{
    LOG_INFO << "pid= " << getpid();
    if (argc > 1)
    {
        g_file = argv[1];

        chaoxi::net::EventLoop loop;
        chaoxi::net::InetAddress listenerAddr(2021);
        chaoxi::net::TcpServer server(&loop, listenerAddr, "FileTransferServer");
        server.setConnectionCallback(onConnection);
        server.start();
        loop.loop();
    }
    else
    {
        std::println(stderr, "Usage: {} <file_path>", argv[0]);
    }
}