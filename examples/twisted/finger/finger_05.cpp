// 读取用户名、输出错误信息，然后断开连接。
// 　如果读到一行以\r\n结尾的消息，就发送一条出错信息，然后断开连接。安全问题同上。

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Buffer.hpp"
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
    server.setMessageCallback(
        [](const TcpConnectionPtr& conn, Buffer& buf, Timestamp receivedTime)
        {
            if (buf.findCRLF())
            {
                conn->send("No such user\r\n");
                conn->shutdown();
            }
        });
    server.start();
    loop.loop();
}