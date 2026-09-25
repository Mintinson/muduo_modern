# examples/simple 的协程版

本目录是 `examples/simple` 中“可以用协程改造”的例子对应的协程实现，
每个可执行文件都与同级目录下的回调版功能等价，只是改用
`chaoxi::coro`（`Task` / `AsyncSocket` / `AsyncAcceptor` / `TcpServer`）
来表达异步流程。

| 协程版 target | 回调版 | 用到的 coro 能力 |
|---|---|---|
| `coro_simple_echo` | `simple_echo` | `TcpServer` + `readSome` / `writeAll` |
| `coro_simple_discard` | `simple_discard` | `TcpServer` + `readSome` |
| `coro_simple_daytime` | `simple_daytime` | `TcpServer` + `writeAll` / `shutdownWrite` |
| `coro_simple_time_server` | `simple_time` | `TcpServer` + `writeAll`，二进制定长帧 |
| `coro_simple_time_client` | `simple_time_client` | `AsyncSocket::connect` + `readExactly` |
| `coro_simple_chargen` | `simple_chargen` | `TcpServer` + `writeAll` + `yield` |
| `coro_simple_acceptor` | `s04/acceptor_1` | `AsyncAcceptor::accept` |
| `coro_simple_connector` | `s04/connector_1` | `AsyncSocket::connect` |

## 与回调版的差异

- 回调版把一次连接拆散到 `onConnection` / `onMessage` /
  `onWriteComplete` 等多个回调中；协程版把这些状态收进**一个连接协程**，
  用 `co_await` 表达“等到可读 / 等到写完”，局部变量天然就是连接状态。
- 回调版需要自己维护 `Buffer`（粘包、半包、读写水位）；协程版用
  `readExactly` 读定长帧、`readSome` 读任意长度，`writeAll` 负责写完整。
- `TcpServer` 的连接处理器返回值是 `Task<void>`，由 `spawn` 派生成独立
  协程；连接处理器抛出的异常通过 `TcpServer::setErrorHandler` 上报，
  未设置时 `spawn` 会 `std::terminate()`。

## 构建与运行

```bash
cmake -S . -B build -DCHAOXI_BUILD_CORO=ON -DCHAOXI_BUILD_EXAMPLES=ON
cmake --build build -j
```

```bash
# echo
./build/examples/simple/coro_example/coro_simple_echo 8888
# 另一个终端
nc 127.0.0.1 8888

# time 服务端 + 协程客户端
./build/examples/simple/coro_example/coro_simple_time_server 8888
./build/examples/simple/coro_example/coro_simple_time_client 127.0.0.1 8888

# acceptor / connector
./build/examples/simple/coro_example/coro_simple_acceptor
./build/examples/simple/coro_example/coro_simple_connector 127.0.0.1 9981
```

> `coro_simple_connector` 默认连接 `127.0.0.1:10275`（与回调版
> `connector_1.cpp` 一致），可先用 `coro_simple_acceptor` 或任意监听者
> 占住端口，也可以通过命令行参数指定 `host port`。

## 约束提醒

- `EventLoop` 必须比归属于它的协程和异步对象活得更久。
- 异步对象应在其 `EventLoop` 线程创建；`close()` 可以从其他线程调用。
- `TcpServer::stop()` 不会强制取消已经启动的连接协程。
