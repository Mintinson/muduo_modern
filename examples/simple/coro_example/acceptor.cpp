// 协程版 Acceptor 示例：与 examples/simple/s04/acceptor_1.cpp 等价。
//
// 回调版给每个 Acceptor 注册 newConnection 回调，收到连接后 write + close；
// 协程版把 accept 写成循环：co_await acceptor.accept() 拿到连接，
// 直接在同一个协程里 writeAll，无需再拆出回调函数。

#include "Bytes.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chaoxi/coro/Chaoxi.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"

#include <cstdint>
#include <exception>
#include <print>
#include <string_view>
#include <system_error>

namespace
{

chaoxi::coro::Task<void> serveGreeting(chaoxi::coro::AsyncAcceptor& acceptor,
                                       std::string_view message)
{
    while (true)
    {
        auto connection = co_await acceptor.accept();
        std::println("newConnection(): accept a new connection from {}",
                     connection.peerAddress.toIpPort());

        co_await connection.socket.writeAll(coro_example::asBytes(message));
        connection.socket.close();
    }
}

}  // namespace

int main()
{
    std::println("main (): pid={}\n", chaoxi::process_info::pid());

    chaoxi::net::EventLoop loop;

    chaoxi::coro::AsyncAcceptor acceptor1{loop, chaoxi::net::InetAddress{9981}};
    chaoxi::coro::AsyncAcceptor acceptor2{loop, chaoxi::net::InetAddress{9999}};

    chaoxi::coro::spawn(loop, serveGreeting(acceptor1, "How are you?\n"));
    chaoxi::coro::spawn(loop, serveGreeting(acceptor2, "What's up man!\n"));

    loop.loop();
}
