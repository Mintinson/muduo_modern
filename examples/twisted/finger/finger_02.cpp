// 接受新连接: 在1079端口侦听新连接，接受连接之后什么都不做，程序空等。

#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

using namespace chaoxi;
using namespace chaoxi::net;

int main()
{
    EventLoop loop;
    TcpServer server(&loop, InetAddress(1079), "Finger");
    server.start();
    loop.loop();
}
