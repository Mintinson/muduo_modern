// 协程版 EchoServer：与 examples/simple/echo 的回调版功能等价。
//
// 回调版在 onMessage 中把收到的数据原样 send 回去；
// 协程版把“读 -> 写”放进一个连接协程，用 co_await 表达等待，
// 读写循环里只关心字节本身，不再需要 TcpConnection / Buffer。

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/coro/Chaoxi.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace
{

constexpr std::size_t kBufferSize = 4096;

chaoxi::coro::Task<void> echoConnection(chaoxi::coro::AsyncSocket socket,
                                        chaoxi::net::InetAddress peer)
{
    LOG_INFO << "EchoServer - " << peer.toIpPort() << " is UP";

    std::array<std::byte, kBufferSize> buffer{};
    try
    {
        while (true)
        {
            const std::size_t size = co_await socket.readSome(buffer);
            if (size == 0)
            {
                break;  // 对端关闭连接，readSome 返回 0。
            }
            FLOG_INFO("echo {} bytes, data received from {}", size,
                      peer.toIpPort());
            co_await socket.writeAll(
                std::span<const std::byte>{buffer.data(), size});
        }
    }
    catch (const std::system_error& error)
    {
        // 对端直接 RST 时会抛 connection_reset；它与读到 EOF 一样，
        // 都表示连接结束，不应该当成错误上报。
        if (error.code() != std::errc::connection_reset)
        {
            throw;
        }
    }

    LOG_INFO << "EchoServer - " << peer.toIpPort() << " is DOWN";
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
        { return echoConnection(std::move(socket), std::move(peer)); }};

    // 连接协程抛出的异常会交给这里，避免 spawn 默认 std::terminate()。
    server.setErrorHandler(
        [](std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR << "echo connection failed: " << exception.what();
            }
        });

    LOG_INFO << "EchoServer listening on " << server.localAddress().toIpPort();
    chaoxi::coro::spawn(loop, server.run());
    loop.loop();
}
