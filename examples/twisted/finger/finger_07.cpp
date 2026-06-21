// 从空的UserMap里查找用户。
// 　从一行消息中拿到用户名（L30），在UserMap里查找，然后返回结果。安全问题同上。

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Buffer.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <string>
#include <unordered_map>

using namespace chaoxi;
using namespace chaoxi::net;

std::unordered_map<std::string, std::string> userMap;

std::string getUser(const std::string& user)
{
    if (auto it = userMap.find(user); it != userMap.end())
    {
        return it->second;
    }
    return "No such user!";
}

int main()
{
    userMap["hello"] = "Mike";
    EventLoop loop;
    TcpServer server(&loop, InetAddress(1079), "Finger");
    server.setMessageCallback(
        [](const TcpConnectionPtr& conn, Buffer& buf, Timestamp receivedTime)
        {
            auto* crlf = buf.findCRLF();
            if (crlf)
            {
                std::string user(buf.peek(), crlf);
                conn->send(getUser(user) + "\r\n");
                buf.retrieveUntil(crlf + 2);
                conn->shutdown();
            }
        });
    server.start();
    loop.loop();
}