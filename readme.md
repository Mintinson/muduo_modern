# chaoxi

`chaoxi` 是一个基于 C++ 的网络库练习项目，整体结构参考了 muduo 的常见组织方式。

## 目录结构

- `base/`：基础工具和通用组件
- `net/`：网络相关核心代码
- `examples/`：示例程序
- `tests/`：测试代码

## 构建

项目使用 CMake 构建，建议在 `build/` 目录下进行编译。

### 依赖

- 核心库仅需支持 C++23 的编译器
- `benchmark` 依赖 `google-benchmark`
- 单元测试依赖 `gtest`
- 以上依赖均通过 CMake 的 `FetchContent` 自动拉取，无需手动额外配置

### 构建步骤

```bash
mkdir build
cd build

# 配置完整项目：库、examples、tests、benchmarks
cmake -S ..

# 如果只想构建 chaoxi 库，可以关闭其他选项
# cmake -S .. -DCHAOXI_BUILD_EXAMPLES=OFF -DCHAOXI_BUILD_TESTS=OFF -DCHAOXI_BUILD_BENCHMARKS=OFF

cmake --build .
```

### Windows（Visual Studio）

Windows 使用 Winsock、基于 AFD 的 `wepoll` 事件分发、loopback socket 跨线程唤醒，以及由事件循环超时驱动的定时器 fallback。Linux 仍默认使用原有的 epoll/eventfd/timerfd 实现。

在 PowerShell 7 中先加载 Visual Studio x64 开发环境，再配置和构建：

```powershell
Import-Module "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\18\Community" -SkipAutomaticLocation -DevCmdArguments "-arch=amd64"

cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows 默认构建可移植的 examples。直接演示 `timerfd`、`epoll/pipe2`、Unix `socketpair` 等 Linux 内核接口的目标仅在 Linux 上生成。

Windows 默认通过 `FetchContent` 获取固定版本 v1.5.8 的
[`wepoll`](https://github.com/piscisaureus/wepoll)。它提供与 epoll 接近的
ADD/MOD/DEL/WAIT 就绪通知模型，因此现有的 `EventLoop`、`Poller` 和
`Channel` 结构不需要改变。可通过 `-DCHAOXI_WINDOWS_USE_WEPOLL=OFF`
切回兼容性的 `select` 后端。IOCP 属于完成通知模型；若后续引入，更适合作为
独立 Proactor 后端实现异步 accept/read/write，而不是伪装成 Reactor。

## 作为第三方库使用

如果你在一个新项目里使用它，推荐把这个仓库作为子目录接入，例如放在 `third_party/chaoxi/`，然后在你自己的 `CMakeLists.txt` 里这样写：

```cmake
set(CHAOXI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CHAOXI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CHAOXI_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/chaoxi)
target_link_libraries(your_target PUBLIC chaoxi::chaoxi)
```

如果你是通过 `git clone` 方式引入，也可以先把仓库放到项目目录下，再用同样的方式通过 `add_subdirectory()` 接入。这样只会编译库本身，`examples/`、`tests/` 和 `benchmark/` 都会保持关闭。

```cmake
add_subdirectory(chaoxi)
target_link_libraries(your_target PUBLIC chaoxi::chaoxi)
```

## 运行

编译完成后，可根据 `examples/` 或 `tests/` 中对应的目标程序运行。

## 协程 v2

项目提供可选的 `chaoxi::v2` 无栈协程接口，默认开启，不影响原有 callback
版本。关闭方式：

```bash
cmake -S . -B build-v2 -DCHAOXI_BUILD_V2=OFF
cmake --build build-v2 -j
```

协程版包含 `Task<T>`、EventLoop 调度、异步定时器、AsyncFd、AsyncSocket、
AsyncAcceptor 和协程 TcpServer。API、示例与生命周期约束见
[`chaoxi/v2/README.md`](chaoxi/v2/README.md)。

### 单元测试

所有 GoogleTest 用例都已注册到 CTest，可并行运行：

```bash
ctest --test-dir build --output-on-failure -j
```

测试覆盖日志、队列、文件与线程工具、Buffer、定时器、地址与 Socket、
EventLoop/EPoll、TCP 连接和 HTTP 协议处理。其中并发队列测试会校验多生产者、
多消费者场景下的无丢失、无重复和对象发布完整性。

### 性能基准

性能数据应使用 Release 构建：

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHAOXI_BUILD_EXAMPLES=OFF \
  -DCHAOXI_BUILD_TESTS=OFF \
  -DCHAOXI_BUILD_BENCHMARKS=ON
cmake --build build-release -j

./build-release/benchmark/QueueComparison_bench
./build-release/benchmark/Buffer_bench
./build-release/benchmark/Http_bench
```

`QueueComparison_bench` 在相同负载下对比互斥锁队列和有界无锁队列；
`Buffer_bench` 覆盖追加、扩容、整数编解码与 CRLF 扫描；
`Http_bench` 覆盖完整请求解析和不同 body 大小的响应序列化。

## 文档生成

本仓库使用 Doxygen 注释风格。若系统已安装 `doxygen` 和 `graphviz`，可以执行：

```bash
doxygen Doxyfile
```

生成后的 HTML 文档位于 `docs/` 目录下。

## 说明

该仓库主要用于学习和实验网络编程，具体实现细节以各个模块为准。

## 参考

- https://github.com/chenshuo/muduo
