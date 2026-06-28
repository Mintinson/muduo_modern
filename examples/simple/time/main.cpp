#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <string>

#include <unistd.h>

#include "time.hpp"

int main(int argc, char** argv)
{
    std::uint16_t port = 8888;
    if (argc > 1)
    {
        port = std::stoul(argv[1]);
    }
    LOG_INFO << "pid = " << getpid();
    chaoxi::net::EventLoop loop;
    chaoxi::net::InetAddress listenAddr(port);
    TimeServer server(&loop, listenAddr);
    server.start();
    loop.loop();
}
