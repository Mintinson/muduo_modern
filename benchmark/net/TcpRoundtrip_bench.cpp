#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Buffer.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThread.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/SocketOps.hpp"
#include "chaoxi/net/TcpConnection.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <benchmark/benchmark.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace
{
constexpr std::uint16_t kBenchmarkPort = 19'182;

class EchoServer final
{
public:
    EchoServer()
        : thread_(
              [this](chaoxi::net::EventLoop* loop)
              {
                  loop_ = loop;
                  server_ = std::make_unique<chaoxi::net::TcpServer>(
                      loop, chaoxi::net::InetAddress(kBenchmarkPort, true),
                      "TcpRoundtripBenchmark");
                  server_->setConnectionCallback(
                      [](const auto& connection)
                      {
                          if (connection->connected())
                          {
                              connection->setTcpNoDelay(true);
                          }
                      });
                  server_->setMessageCallback(
                      [](const auto& connection, chaoxi::net::Buffer& buffer,
                         chaoxi::Timestamp)
                      { connection->send(std::move(buffer)); });
                  server_->start();
              })
    {
        chaoxi::Logger::setLogLevel(chaoxi::Logger::LogLevel::WARN);
        thread_.startLoop();
    }

    ~EchoServer()
    {
        std::promise<void> stopped;
        auto done = stopped.get_future();
        loop_->queueInLoop(
            [this, &stopped]
            {
                server_.reset();
                stopped.set_value();
                loop_->quit();
            });
        done.wait();
    }

    EchoServer(const EchoServer&) = delete;
    EchoServer& operator=(const EchoServer&) = delete;

private:
    chaoxi::net::EventLoopThread thread_;
    chaoxi::net::EventLoop* loop_{};
    std::unique_ptr<chaoxi::net::TcpServer> server_;
};

EchoServer& echoServer()
{
    static EchoServer server;
    return server;
}

class BlockingClient final
{
public:
    BlockingClient() : socket_(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))
    {
        if (socket_ == chaoxi::net::kInvalidSocket)
        {
            throw std::runtime_error("socket failed");
        }
        constexpr int enabled = 1;
        ::setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, &enabled,
                     sizeof(enabled));
        const chaoxi::net::InetAddress server("127.0.0.1", kBenchmarkPort);
        if (::connect(socket_, server.getSockAddr(), sizeof(sockaddr_in)) != 0)
        {
            chaoxi::net::sockets::close(socket_);
            throw std::runtime_error("connect failed");
        }
    }

    ~BlockingClient() { chaoxi::net::sockets::close(socket_); }

    void roundTrip(std::string_view payload)
    {
        writeAll(payload);
        std::size_t received{};
        while (received < payload.size())
        {
            const auto result = ::recv(
                socket_, receiveBuffer_.data(),
                std::min(receiveBuffer_.size(), payload.size() - received), 0);
            if (result > 0)
            {
                received += static_cast<std::size_t>(result);
            }
            else if (result < 0 && errno == EINTR)
            {
                continue;
            }
            else
            {
                throw std::runtime_error("recv failed");
            }
        }
    }

private:
    void writeAll(std::string_view payload)
    {
        std::size_t written{};
        while (written < payload.size())
        {
            const auto result = ::send(socket_, payload.data() + written,
                                       payload.size() - written, MSG_NOSIGNAL);
            if (result > 0)
            {
                written += static_cast<std::size_t>(result);
            }
            else if (result < 0 && errno == EINTR)
            {
                continue;
            }
            else
            {
                throw std::runtime_error("send failed");
            }
        }
    }

    chaoxi::net::SocketHandle socket_;
    std::array<char, 64 * 1'024> receiveBuffer_{};
};

void tcpRoundTrip(benchmark::State& state)
{
    benchmark::DoNotOptimize(echoServer());
    const auto payloadSize = static_cast<std::size_t>(state.range(0));
    const std::string payload(payloadSize, 'x');
    BlockingClient client;

    for (auto _ : state)
    {
        (void)_;
        client.roundTrip(payload);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(payloadSize * 2));
}

BENCHMARK(tcpRoundTrip)
    ->Name("Tcp/RoundTrip")
    ->Arg(64)
    ->Arg(1'024)
    ->Arg(16'384)
    ->Arg(65'536)
    ->Arg(1'048'576)
    ->UseRealTime();
}  // namespace
