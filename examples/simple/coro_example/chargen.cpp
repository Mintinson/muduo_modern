// 协程版 ChargenServer：与 examples/simple/chargen 的回调版功能等价。
//
// 回调版在 onConnection 里 send(message_)，再靠 onWriteComplete 回调不断
// 续发；协程版把“续发”写成一个 while 循环：writeAll 返回即表示这一轮写完，
// 然后 co_await yield(loop) 让出执行权，效果与写完成回调被再次触发一致，
// 同时保证 loop 还能处理其他连接和定时器。

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/coro/Chaoxi.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <string>
#include <system_error>
#include <utility>

#include "Bytes.hpp"

namespace
{

std::string makeChargenMessage()
{
    std::string line;
    for (int i = 33; i < 127; ++i)
    {
        line.push_back(static_cast<char>(i));
    }
    line += line;

    std::string message;
    for (std::size_t i = 0; i < 127 - 33; ++i)
    {
        message += line.substr(i, 72) + '\n';
    }
    return message;
}

struct Throughput
{
    std::int64_t transferred{};
};

chaoxi::coro::Task<void> chargenConnection(chaoxi::net::EventLoop& loop,
                                           chaoxi::coro::AsyncSocket socket,
                                           chaoxi::net::InetAddress peer,
                                           Throughput& throughput

)
{
    socket.setTcpNoDelay(true);
    const std::string message = makeChargenMessage();

    // transferred = 0;
    try
    {
        while (true)
        {
            co_await socket.writeAll(coro_example::asBytes(message));
            throughput.transferred += static_cast<std::int64_t>(message.size());

            // 让出执行权：既等价于“写完成后再次发送”，也避免独占 loop。
            co_await chaoxi::coro::yield(loop);
        }
    }
    catch (const std::system_error& error)
    {
        // 对端断开或被取消时属于正常收尾。
        LOG_INFO << "ChargenServer - " << peer.toIpPort()
                 << " disconnected: " << error.what();
    }

    // LOG_INFO << "ChargenServer - " << peer.toIpPort() << " transferred "
    //          << transferred << " bytes";
}

chaoxi::coro::Task<void> printThroughput(chaoxi::net::EventLoop& loop,
                                         Throughput& throughput)
{
    auto previousTime = chaoxi::Timestamp::clock::now();

    while (true)
    {
        co_await chaoxi::coro::sleepFor(loop, std::chrono::seconds{3});

        const auto now = chaoxi::Timestamp::clock::now();
        const double seconds =
            std::chrono::duration<double>(now - previousTime).count();

        const auto transferred =
            std::exchange(throughput.transferred, std::int64_t{0});

        LOG_INFO << std::format("{:4.3f} MiB/s",
                                static_cast<double>(transferred) / seconds /
                                    1024.0 / 1024.0);

        previousTime = now;
    }
}
}  // namespace

int main(int argc, char** argv)
{
    std::uint16_t port = 2019;
    if (argc > 1)
    {
        port = static_cast<std::uint16_t>(std::stoul(argv[1]));
    }

    LOG_INFO << "pid = " << chaoxi::process_info::pid();

    chaoxi::net::EventLoop loop;
    Throughput throughput;
    chaoxi::coro::TcpServer server{
        loop, chaoxi::net::InetAddress{port},
        [&loop, &throughput](chaoxi::coro::AsyncSocket socket,
                             chaoxi::net::InetAddress peer)
        {
            return chargenConnection(loop, std::move(socket), std::move(peer),
                                     throughput);
        }};

    server.setErrorHandler(
        [](std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR << "chargen connection failed: " << exception.what();
            }
        });

    LOG_INFO << "ChargenServer listening on "
             << server.localAddress().toIpPort();
    chaoxi::coro::spawn(loop, server.run());
    chaoxi::coro::spawn(loop, printThroughput(loop, throughput));
    loop.loop();
}
