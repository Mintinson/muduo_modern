// 协程版 TimeClient：与 examples/simple/time_client.cpp 的回调版功能等价。
//
// 回调版依赖 TcpClient + onMessage 里手工判断“缓冲区是否够 sizeof(size_t)”；
// 协程版用 AsyncSocket::connect + readExactly，一次 co_await 就读满定长帧，
// 不再需要自己维护 Buffer 的粘包判断。

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/coro/Chaoxi.hpp"
#include "chaoxi/net/Endian.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <print>
#include <string>

namespace
{

chaoxi::coro::Task<void> runClient(chaoxi::net::EventLoop& loop,
                                   chaoxi::net::InetAddress serverAddr)
{
    auto socket = co_await chaoxi::coro::AsyncSocket::connect(loop, serverAddr);

    std::array<std::byte, sizeof(std::size_t)> buffer{};
    co_await socket.readExactly(buffer);

    std::size_t bigEndian = 0;
    std::memcpy(&bigEndian, buffer.data(), sizeof(bigEndian));
    const std::size_t time = chaoxi::net::sockets::networkToHost(bigEndian);
    const auto timepoint =
        chaoxi::Timestamp::clock::time_point(chaoxi::Timestamp::duration(time));

    LOG_INFO << "Server time = " << time << ", "
             << std::format("{:%Y-%m-%d %H:%M:%S}", timepoint);

    loop.quit();
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc <= 1)
    {
        std::println(stderr, "Usage: {} host_ip [port]", argv[0]);
        return 1;
    }

    std::uint16_t port = 8888;
    if (argc > 2)
    {
        port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    }

    LOG_INFO << "pid = " << chaoxi::process_info::pid();

    chaoxi::net::EventLoop loop;
    chaoxi::net::InetAddress serverAddr(argv[1], port);

    chaoxi::coro::spawn(
        loop, runClient(loop, serverAddr),
        [&loop](std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR << "time client failed: " << exception.what();
            }
            loop.quit();
        });

    loop.loop();
}
