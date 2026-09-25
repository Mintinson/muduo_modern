// 协程版 DayTimeServer：与 examples/simple/daytime 的回调版功能等价。
//
// 回调版在 onConnection 中 conn->send(当前时间) + conn->shutdown()；
// 协程版在连接协程里一次 writeAll + shutdownWrite，语义一一对应。

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/coro/Chaoxi.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <cstdint>
#include <exception>
#include <format>
#include <string>
#include <utility>

#include "Bytes.hpp"

namespace
{

chaoxi::coro::Task<void> daytimeConnection(chaoxi::coro::AsyncSocket socket,
                                           chaoxi::net::InetAddress peer)
{
    LOG_INFO << "DayTime server - " << peer.toIpPort() << " is UP";
    const std::string line =
        std::format("{:%Y-%m-%d %H:%M:%S}\n", chaoxi::Timestamp::clock::now());

    co_await socket.writeAll(coro_example::asBytes(line));
    socket.shutdownWrite();  // 半关闭：发送 FIN，与回调版的 conn->shutdown()
                             // 等价。

    LOG_INFO << "DayTimeServer - served " << peer.toIpPort();
    LOG_INFO << "DayTime - " << peer.toIpPort() << " is DOWN";

    std::array<std::byte, 1024> buffer{};

    while (true)
    {
        const std::size_t size = co_await socket.readSome(buffer);
        if (size == 0)
        {
            break;  
        }
        FLOG_INFO("Still received {} bytes, data received from {}", size, peer.toIpPort());

    }
}

}  // namespace

int main(int argc, char** argv)
{
    std::uint16_t port = 8888;
    if (argc > 1)
    {
        port = static_cast<std::uint16_t>(std::stoul(argv[1]));
    }

    LOG_INFO << "pid = " << chaoxi::process_info::pid();

    chaoxi::net::EventLoop loop;
    chaoxi::coro::TcpServer server{
        loop, chaoxi::net::InetAddress{port},
        [](chaoxi::coro::AsyncSocket socket, chaoxi::net::InetAddress peer)
        { return daytimeConnection(std::move(socket), std::move(peer)); }};

    server.setErrorHandler(
        [](std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR << "daytime connection failed: " << exception.what();
            }
        });

    LOG_INFO << "DayTimeServer listening on "
             << server.localAddress().toIpPort();
    chaoxi::coro::spawn(loop, server.run());
    loop.loop();
}
