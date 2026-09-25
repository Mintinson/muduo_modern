// 协程版 DiscardServer：与 examples/simple/discard 的回调版功能等价。
//
// 回调版在 onMessage 里把数据从 Buffer 里取出后直接丢弃；
// 协程版用一个连接协程持续 readSome，读完即丢，逻辑更直观。

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/coro/Chaoxi.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>

namespace
{

constexpr std::size_t kBufferSize = 4096;

chaoxi::coro::Task<void> discardConnection(chaoxi::coro::AsyncSocket socket,
                                           chaoxi::net::InetAddress peer)
{
    LOG_INFO << "DiscardServer - " << peer.toIpPort() << " is UP";

    std::array<std::byte, kBufferSize> buffer{};
    std::int64_t discarded = 0;
    while (true)
    {
        const std::size_t size = co_await socket.readSome(buffer);
        if (size == 0)
        {
            break;
        }
        discarded += static_cast<std::int64_t>(size);
    }

    LOG_INFO << "DiscardServer - " << peer.toIpPort() << " discarded "
             << discarded << " bytes";
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
        { return discardConnection(std::move(socket), std::move(peer)); }};

    server.setErrorHandler(
        [](std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR << "discard connection failed: " << exception.what();
            }
        });

    LOG_INFO << "DiscardServer listening on "
             << server.localAddress().toIpPort();
    chaoxi::coro::spawn(loop, server.run());
    loop.loop();
}
