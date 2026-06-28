#include "chaoxi/base/CurrentThread.hpp"
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <unistd.h>

#include "examples/simple/chargen/chargen.hpp"
#include "examples/simple/daytime/daytime.hpp"
#include "examples/simple/discard/discard.hpp"
#include "examples/simple/echo/echo.hpp"
#include "examples/simple/time/time.hpp"

using namespace chaoxi::net;
using namespace chaoxi;

int main()
{
    LOG_INFO << "pid = " << getpid() << ", tid = " << CurrentThread::tid();

    EventLoop loop;

    ChargenServer chargenServer(&loop, InetAddress(2017));
    chargenServer.start();  // 启动后才会监听端口，否则无法接收客户端连接
    DayTimeServer daytimeServer(&loop, InetAddress(2018));
    daytimeServer.start();
    DiscardServer discardServer(&loop, InetAddress(2019));
    discardServer.start();
    EchoServer echoServer(&loop, InetAddress(2020));
    echoServer.start();
    TimeServer timeServer(&loop, InetAddress(2021));
    timeServer.start();

    loop.loop();
}