// 读取用户名，然后断开连接。
// 　如果读到一行以\r\n结尾的消息，就断开连接。
// 注意这段代码有安全问题，如果恶意客户端不断发送数据而不换行，会撑爆服务端的内存。
// 另外，Buffer::findCRLF()是线性查找，如果客户端每次发一个字节，服务端的时间复杂度为O(N2
// )，会消耗CPU资源。

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
                conn->shutdown();
            }
        });
    server.start();
    loop.loop();
}
