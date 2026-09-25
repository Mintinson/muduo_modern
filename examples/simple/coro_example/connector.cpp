// 协程版 Connector 示例：与 examples/simple/s04/connector_1.cpp 等价。
//
// 回调版用 Connector + newConnectionCallback；协程版直接
// co_await AsyncSocket::connect(...)，连接结果就是协程的返回值，
// 失败则以 std::system_error 抛出，不需要额外回调。

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/coro/Chaoxi.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <cstdint>
#include <exception>
#include <print>
#include <string>
#include <system_error>

namespace
{

chaoxi::coro::Task<void> connectAndReport(chaoxi::net::EventLoop& loop,
                                          chaoxi::net::InetAddress address)
{
    try
    {
        auto socket = co_await chaoxi::coro::AsyncSocket::connect(loop, address);

        const auto peer = chaoxi::net::sockets::getPeerAddr(socket.nativeHandle());
        std::println("Connected to fd {} address {}", socket.nativeHandle(),
                     chaoxi::net::sockets::toIpPort(
                         reinterpret_cast<const sockaddr*>(&peer)));
    }
    catch (const std::system_error& error)
    {
        LOG_ERROR << "connect failed: " << error.what();
    }

    loop.quit();
}

}  // namespace

int main(int argc, char** argv)
{
    std::string ip = "127.0.0.1";
    std::uint16_t port = 10275;
    if (argc > 1)
    {
        ip = argv[1];
    }
    if (argc > 2)
    {
        port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    }

    chaoxi::net::EventLoop loop;
    chaoxi::net::InetAddress address(ip, port);

    chaoxi::coro::spawn(loop, connectAndReport(loop, address),
                        [&loop](std::exception_ptr error)
                        {
                            try
                            {
                                std::rethrow_exception(error);
                            }
                            catch (const std::exception& exception)
                            {
                                LOG_ERROR << "connector failed: "
                                          << exception.what();
                            }
                            loop.quit();
                        });

    loop.loop();
}
