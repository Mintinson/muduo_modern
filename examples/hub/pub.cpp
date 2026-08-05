#include "chaoxi/base/CurrentThread.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThread.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <print>
#include <string>

#include "pubsub.hpp"

namespace
{

// 单次发布模式的连接回调没有额外上下文参数。这个小型命令行示例用全局变量
// 把 main 中解析出的参数交给回调；实际库代码更适合用捕获 lambda 或业务对象
// 保存这些状态。
chaoxi::net::EventLoop* g_loop = nullptr;
std::string g_topic;
std::string g_content;

void onConnection(pubsub::PubSubClient* client)
{
    if (client->connected())
    {
        // TCP 连接建立后发布一次，然后发起优雅关闭。TcpConnection 会先发送
        // 已排队的数据，再 shutdown 写端，因此不会因为立即 stop() 丢消息。
        client->publish(g_topic, g_content);
        client->stop();
    }
    else
    {
        // 断开回调标志着一次发布流程结束，让 main 中的 loop.loop() 返回。
        g_loop->quit();
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc == 4)
    {
        // argv[1] 使用 host:port 形式。find(':') 足以处理本示例的 IPv4 地址；
        // 若要支持裸 IPv6 地址，应采用 [::1]:9999 之类的格式和专门解析器。
        pubsub::string hostport = argv[1];
        const std::size_t colon = hostport.find(':');
        if (colon != std::string::npos)
        {
            const std::string hostip = hostport.substr(0, colon);
            const auto port =
                static_cast<uint16_t>(atoi(hostport.c_str() + colon + 1));
            g_topic = argv[2];
            g_content = argv[3];

            std::string name = chaoxi::process_info::username() + "@" +
                               chaoxi::process_info::hostname();
            name += ":" + chaoxi::process_info::pidString();

            if (g_content == "-")
            {
                // stdin 的 getline() 是阻塞调用。若 EventLoop 也放在主线程，等待
                // 用户输入期间网络事件就得不到处理。因此流式模式把 Reactor
                // 放入 EventLoopThread，主线程只负责读取终端。
                chaoxi::net::EventLoopThread loopThread;
                g_loop = loopThread.startLoop();
                pubsub::PubSubClient client(
                    g_loop, chaoxi::net::InetAddress(hostip, port), name);
                client.start();

                std::string line;
                while (std::getline(std::cin, line))
                {
                    // PubSubClient::publish 获取线程安全的连接快照，而
                    // TcpConnection::send 会把跨线程消息投递到 EventLoop。
                    client.publish(g_topic, line);
                }

                // EOF 后优雅断开。短暂等待让断开事件回到后台 EventLoop；这是
                // 命令行示例的简化收尾，长期运行的应用通常使用完成通知来同步。
                client.stop();
                chaoxi::CurrentThread::sleepUsec(1000 * 1000);
            }
            else
            {
                // 单次模式没有阻塞输入，直接让 EventLoop 占用主线程即可。
                chaoxi::net::EventLoop loop;
                g_loop = &loop;
                pubsub::PubSubClient client(
                    g_loop, chaoxi::net::InetAddress(hostip, port), name);
                client.setConnectionCallback(onConnection);
                client.start();
                loop.loop();
            }
        }
        else
        {
            std::println("Usage: {} hub_ip:port topic content", argv[0]);
        }
    }
    else
    {
        std::println("Usage: {} hub_ip:port topic content\nRead contents from "
                     "stdin:\n  {} hub_ip:port topic -",
                     argv[0], argv[0]);
    }
}
