#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <cstdint>
#include <cstdlib>
#include <print>
#include <string>
#include <vector>

#include "pubsub.hpp"

using namespace chaoxi;
using namespace chaoxi::net;
using namespace pubsub;

namespace
{

// 与 pub.cpp 类似，这些变量是命令行示例在 main 与 C 风格回调之间传递状态的
// 简化手段。它们只由 EventLoop 所在线程访问，因此不需要互斥锁。
EventLoop* g_loop = nullptr;
std::vector<string> g_topics;

void onPublished(const string& topic, const string& content, Timestamp)
{
    // 第三个参数是消息到达 EventLoop 的时间。本示例只显示业务内容，故省略它。
    std::println("{}: {}", topic, content);
}

void onConnection(PubSubClient* client)
{
    if (client->connected())
    {
        // 必须等 TCP 连接建立后再发送 sub 命令；start() 本身只发起异步连接。
        for (const auto& topic : g_topics)
        {
            client->subscribe(topic, onPublished);
        }
    }
    else
    {
        // hub 关闭或网络断开后退出事件循环，让订阅进程自然结束。
        g_loop->quit();
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc > 2)
    {
        // 参数 2..N 都是主题，因此一个 TCP 连接可以同时订阅多个 topic。
        string hostport = argv[1];
        const std::size_t colon = hostport.find(':');
        if (colon != string::npos)
        {
            const string hostip = hostport.substr(0, colon);
            const uint16_t port =
                static_cast<uint16_t>(atoi(hostport.c_str() + colon + 1));
            for (int i = 2; i < argc; ++i)
            {
                g_topics.emplace_back(argv[i]);
            }

            EventLoop loop;
            g_loop = &loop;

            // 连接名只用于日志和诊断，组合用户名、主机名和 pid 后容易区分
            // 同一台机器上运行的多个 sub 进程。
            string name =
                process_info::username() + "@" + process_info::hostname();
            name += ":" + process_info::pidString();
            PubSubClient client(&loop, InetAddress(hostip, port), name);
            client.setConnectionCallback(onConnection);
            client.start();

            // 阻塞并驱动 TCP 连接、消息解析和用户回调，直到 onConnection 在
            // 断开分支调用 quit()。
            loop.loop();
        }
        else
        {
            std::println("Usage: {} hub_ip:port topic [topic ...]", argv[0]);
        }
    }
    else
    {
        std::println("Usage: {} hub_ip:port topic [topic ...]", argv[0]);
    }
}
