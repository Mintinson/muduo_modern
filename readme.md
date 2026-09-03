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

Windows 当前使用 Winsock + `select` 作为事件分发后端，并使用 loopback socket
完成跨线程唤醒、由事件循环的等待超时驱动定时器。Linux 使用原有的
epoll/eventfd/timerfd 实现。

> **后端状态：** 当前仓库并未启用 wepoll。Windows 实际创建的是
> `PollPoller`，其内部调用 `select()`；`FD_SETSIZE` 当前配置为 1024。因此该
> 后端适合功能验证和中小规模连接，不应把它的扩展性等同于 Linux epoll。

在 PowerShell 7 中先加载 Visual Studio x64 开发环境，再配置和构建：

```powershell
Import-Module "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\18\Community" -SkipAutomaticLocation -DevCmdArguments "-arch=amd64"

cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows 默认构建可移植的 examples。直接演示 `timerfd`、`epoll/pipe2`、Unix `socketpair` 等 Linux 内核接口的目标仅在 Linux 上生成。

源码中保留了 wepoll 接入的注释草案，但它不属于当前构建路径，也没有
`CHAOXI_WINDOWS_USE_WEPOLL` 这一可用选项。若以后引入 wepoll，应同时实现并
测试对应的 `Poller`，再开放构建选项。IOCP 属于完成通知模型；若后续引入，
更适合作为独立 Proactor 后端实现异步 accept/read/write，而不是伪装成
Reactor。

## 作为第三方库使用 (TODO: 更新到使用 find_package)

### 1. 已安装

```cmake
find_package(chaoxi CONFIG REQUIRED)

target_link_libraries(
    app
    PRIVATE
        chaoxi::chaoxi
)
```

### 2. 优先 find，找不到自动 fetch——推荐

```cmake
include(FetchContent)

FetchContent_Declare(
    chaoxi
    GIT_REPOSITORY https://github.com/Mintinson/muduo_modern.git
    GIT_TAG        <tag-or-commit>
    FIND_PACKAGE_ARGS CONFIG
)

FetchContent_MakeAvailable(chaoxi)

target_link_libraries(
    app
    PRIVATE
        chaoxi::chaoxi
)
```

### 3. 强制 Git 源码，并允许使用 `find_package`

```cmake
include(FetchContent)

FetchContent_Declare(
    chaoxi
    GIT_REPOSITORY https://github.com/Mintinson/muduo_modern.git
    GIT_TAG        <tag-or-commit>
    OVERRIDE_FIND_PACKAGE
)

find_package(chaoxi CONFIG REQUIRED)

target_link_libraries(
    app
    PRIVATE
        chaoxi::chaoxi
)
```

### 4. Git Clone 引入

如果你是通过 `git clone` 方式引入，也可以先把仓库放到项目目录下，再用同样的方式通过 `add_subdirectory()` 接入。这样只会编译库本身，`examples/`、`tests/` 和 `benchmark/` 都会保持关闭。

```cmake
add_subdirectory(chaoxi)
target_link_libraries(your_target PUBLIC chaoxi::chaoxi)
```



## 运行

编译完成后，可根据 `examples/` 或 `tests/` 中对应的目标程序运行。

## 协程 v2

项目提供可选的 `chaoxi::v2` 无栈协程接口，默认关闭，不影响原有 callback
版本。启用方式：

```bash
cmake -S . -B build-v2 -DCHAOXI_BUILD_V2=ON
cmake --build build-v2 -j
ctest --test-dir build-v2 -R '^v2\.' --output-on-failure
```

协程版包含 `Task<T>`、EventLoop 调度、异步定时器、AsyncFd、AsyncSocket、
AsyncAcceptor 和协程 TcpServer。API、示例与生命周期约束见
[`chaoxi/v2/README.md`](chaoxi/v2/README.md)。

该选项在 Linux 和 Windows 上都可用。Windows 的协程网络 I/O 复用上述
`select` 后端，因此 `AsyncFd` 在 Windows 上只接受 Winsock socket，且同样
受 `FD_SETSIZE` 限制。

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
