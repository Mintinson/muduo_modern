#include "chaoxi/net/EventLoop.hpp"
#include <print>
#include "chaoxi/base/Logging.hpp"

void print()
{
    LOG_INFO << "Timer expired";
    std::println("Hello, world!");

}

int main()
{
    chaoxi::net::EventLoop loop;
    LOG_INFO << "Starting timer1";
    loop.runAfter(5.0, print);
    loop.loop();
}