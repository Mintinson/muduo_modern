// 主动断开连接。 　接受新连接之后主动断开。

#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

using namespace chaoxi;
using namespace chaoxi::net;

int main()
{
    EventLoop loop;
    TcpServer server(&loop, InetAddress(1079), "Finger");
    server.setConnectionCallback(
        [](const TcpConnectionPtr& conn)
        {
            if (conn->connected())
            {
                conn->shutdown();
            }
        });
    server.start();
    loop.loop();
}
