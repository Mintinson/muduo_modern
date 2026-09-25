// 协程版 TimeServer：与 examples/simple/time 的回调版功能等价。
//
// 回调版在 onConnection 中把当前时间戳以大端 std::size_t 发送后 shutdown；
// 协程版直接 writeAll 发送这 sizeof(std::size_t) 个字节。

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/coro/Chaoxi.hpp"
#include "chaoxi/net/Endian.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <utility>

namespace
{

chaoxi::coro::Task<void> timeConnection(chaoxi::coro::AsyncSocket socket,
                                        chaoxi::net::InetAddress peer)
{
    const auto count = static_cast<std::size_t>(
        chaoxi::Timestamp::clock::now().time_since_epoch().count());
    const auto bigEndian = chaoxi::net::sockets::hostToNetwork(count);

    co_await socket.writeAll(std::as_bytes(std::span{&bigEndian, 1}));
    socket.shutdownWrite();

    LOG_INFO << "TimeServer - served " << peer.toIpPort() << " with " << count;
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
        { return timeConnection(std::move(socket), std::move(peer)); }};

    server.setErrorHandler(
        [](std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR << "time connection failed: " << exception.what();
            }
        });

    LOG_INFO << "TimeServer listening on " << server.localAddress().toIpPort();
    chaoxi::coro::spawn(loop, server.run());
    loop.loop();
}
