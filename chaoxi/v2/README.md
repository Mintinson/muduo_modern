# chaoxi v2 coroutine API

`chaoxi::v2` 是构建在原有 `EventLoop`、`Channel` 和 epoll Reactor
之上的无栈协程接口。它不会替换或修改现有 callback API，两套接口可以在同一
进程中并存。

## 启用

v2 默认不参与构建。配置时显式开启：

```bash
cmake -S . -B build-v2 \
  -DCHAOXI_BUILD_V2=ON \
  -DCHAOXI_BUILD_TESTS=ON
cmake --build build-v2 -j
ctest --test-dir build-v2 -R '^v2\.' --output-on-failure
```

开启后会生成 `chaoxi::v2` target，并通过其 PUBLIC usage requirements
定义 `CHAOXI_HAS_V2=1`。`chaoxi::chaoxi` 汇总 target 也会自动链接 v2。

## API

- `Task<T>`：惰性、move-only 的协程任务，支持返回值和异常传播。
- `spawn()`：在指定 EventLoop 上启动 detached 顶层任务。
- `scheduleOn()` / `yield()`：切换或让出 EventLoop 执行权。
- `sleepFor()` / `sleepUntil()`：基于原有 timerfd/TimerQueue 的定时等待。
- `AsyncFd`：协程化的可读、可写等待及 read/write 操作。
- `AsyncSocket`：异步 connect、精确读取、完整写入和 Socket 选项。
- `AsyncAcceptor`：异步 accept，支持端口 0 和获取实际监听地址。
- `TcpServer`：每个连接由 `Task<void>` handler 处理的协程服务器。

## 示例

```cpp
using namespace chaoxi;

v2::Task<void> echo(v2::AsyncSocket socket, net::InetAddress)
{
    std::array<std::byte, 4096> buffer;
    while (true)
    {
        const std::size_t size = co_await socket.readSome(buffer);
        if (size == 0)
        {
            co_return;
        }
        co_await socket.writeAll(
            std::span<const std::byte>{buffer.data(), size});
    }
}

net::EventLoop loop;
v2::TcpServer server(
    loop,
    net::InetAddress{8080},
    [](v2::AsyncSocket socket, net::InetAddress peer)
    {
        return echo(std::move(socket), std::move(peer));
    });

v2::spawn(loop, server.run());
loop.loop();
```

## 当前约束

- EventLoop 必须比归属于它的协程和异步对象活得更久。
- 异步对象应在其 EventLoop 线程创建；跨线程 `close()` 会自动投递回 loop。
- 每个 fd 同时最多允许一个读等待者和一个写等待者，重复等待返回
  `operation_in_progress`。
- `TcpServer::stop()` 是终止操作，停止后不能重新启动。
- `TcpServer::stop()` 停止 accept，但不会强制取消或等待已经启动的连接 handler；
  应在这些 handler 结束后再销毁 EventLoop。
- v2 暂未提供 `std::stop_token` 风格的通用取消树；关闭 fd/acceptor 会以
  `operation_canceled` 恢复挂起操作。
