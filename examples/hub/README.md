# Hub：基于 Reactor 的发布/订阅示例

这个示例用一个很小的文本协议演示 chaoxi 的典型使用方式：`EventLoop` 负责
事件分发，`TcpServer`/`TcpClient` 负责连接生命周期，业务层只维护主题和订阅
关系。示例包含三个程序：

- `hub`：消息中心，接收订阅、取消订阅和发布命令。
- `sub`：长连接订阅者，可在一条连接上订阅多个主题。
- `pub`：发布者，支持单次发布和从标准输入连续发布。

## 架构

```text
 pub 1 ── pub topic ──┐
 pub 2 ── pub topic ──┤
                      ▼
                 ┌─────────┐
                 │   hub   │
                 │ topics  │
                 └─────────┘
                      │
          ┌───────────┴───────────┐
          ▼                       ▼
       sub A                   sub B
   topic: news            topic: news, sport
```

网络层采用 Reactor 模型。socket 可读、连接建立/断开和定时器到期都由同一个
`EventLoop` 串行分发，所以 hub 的主题表不需要加锁。`pub -` 模式是例外：主
线程阻塞读取 stdin，网络 EventLoop 运行在后台线程；跨线程发送由
`TcpConnection::send()` 投递回网络线程。

## Wire protocol

协议以 CRLF（`\r\n`）划分行：

```text
sub <topic>\r\n
unsub <topic>\r\n
pub <topic>\r\n
<content>\r\n
```

例如，发布 `news` 消息：

```text
pub news\r\n
hello\r\n
```

TCP 没有消息边界，因此一次读取可能得到半条消息，也可能得到多条消息。
`parseMessage()` 只有在整帧到齐后才消费 `Buffer`；数据不足时返回 `kContinue`
并保留所有字节。这是网络协议解析器必须处理的半包/粘包问题。

## Hub 的数据模型

hub 同时维护两个方向的索引：

```text
TcpConnection::context                PubSubServer::topics_
连接 ──► {topic A, topic B}           topic ──► {连接 1, 连接 2}
```

- “连接到主题”用于连接断开时一次找出并清理全部订阅。
- “主题到连接”用于发布消息时快速找到所有接收者。
- `Topic` 保存最后一次发布内容；新订阅者会立即收到这条 retained message。

断开清理时，代码先把连接的订阅集合移动到局部快照，再逐项取消订阅。不能在
range-for 遍历 `unordered_set` 的同时从同一个集合删除当前元素，否则下一次
递增迭代器会访问已经释放的节点。

## 代码地图

阅读数据流之前，可以先明确每个文件负责哪一层：

| 文件 | 职责 | 不负责什么 |
| --- | --- | --- |
| [`codec.cpp`](codec.cpp) | 从 `Buffer` 增量解析一条协议帧 | socket 读取、业务路由 |
| [`pubsub.cpp`](pubsub.cpp) | 把 `TcpClient` 包装成 subscribe/publish API | 保存 hub 的主题表 |
| [`hub.cpp`](hub.cpp) | 维护订阅关系并把发布广播给订阅者 | 主动建立客户端连接 |
| [`sub.cpp`](sub.cpp) | 解析命令行、订阅主题、显示消息 | 协议解析细节 |
| [`pub.cpp`](pub.cpp) | 解析命令行并发布一次或连续发布 | 消息在 hub 内的路由 |

从下到上，可以把一次消息传递看成四层：

```text
Linux socket / epoll
        │
        ▼
Channel + EventLoop + TcpConnection       网络与事件层
        │
        ▼
Buffer + parseMessage()                   协议层
        │
        ▼
PubSubClient / PubSubServer / Topic       业务层
        │
        ▼
pub.cpp / sub.cpp                         命令行应用层
```

下面逐条跟踪数据如何在这些层之间移动。

## 数据流一：启动 hub

命令：

```bash
./build/examples/hub/hub 9999
```

启动过程如下：

1. `main()` 在栈上创建 `EventLoop`。这个对象随后成为 hub 的 Reactor 中心。
2. `PubSubServer` 构造内部 `TcpServer`，并注册两个业务回调：

   - connection callback：连接建立或断开时调用 `onConnection()`；
   - message callback：连接收到字节时调用 `onMessage()`。

3. `runEvery(1.0, ...)` 向同一个 EventLoop 注册 `utc_time` 定时发布任务。
4. `server.start()` 让监听 socket 进入监听状态，并由 `Channel` 关注可读事件。
5. `loop.loop()` 进入循环：等待 epoll、分发 Channel 事件、执行回调，再回到
   epoll 等待。

此时尚未创建业务 `Topic`。`topics_` 采用按需创建策略，第一次收到某个主题的
`sub`、`unsub` 或 `pub` 时才调用 `try_emplace()` 创建对应对象。

```text
main thread
    │
    ├─ EventLoop
    ├─ PubSubServer
    │    └─ TcpServer
    │         └─ Acceptor ── listening fd:9999
    │
    └─ EventLoop::loop()
             └─ epoll_wait(...)
```

## 数据流二：sub 建立连接并发送订阅

命令：

```bash
./build/examples/hub/sub 127.0.0.1:9999 news sport
```

### 1. 客户端建立连接

1. [`sub.cpp`](sub.cpp) 创建自己的 `EventLoop` 和 `PubSubClient`。
2. `PubSubClient` 内部构造 `TcpClient`，并把通用网络回调适配到
   `PubSubClient::onConnection()` 和 `PubSubClient::onMessage()`。
3. `client.start()` 调用 `TcpClient::connect()`。这是非阻塞连接：调用返回不
   代表三次握手已经完成。
4. `Connector` 关注连接 socket 的可写事件。连接完成后，`TcpClient` 创建
   `TcpConnection` 并调用连接回调。
5. 回调最终到达 `sub.cpp::onConnection()`。只有此时 `connected()` 为 true，
   sub 才分别调用 `subscribe("news")` 和 `subscribe("sport")`。

hub 一侧同时发生：监听 fd 变为可读，`Acceptor` 接受连接，`TcpServer` 创建
服务端 `TcpConnection`，然后调用 `PubSubServer::onConnection()`。hub 为这条
连接设置一个空的 `ConnectionSubscription`，存入 `TcpConnection::context`。

### 2. 订阅命令进入网络

以 `news` 为例，客户端构造的实际字节是：

```text
sub news\r\n
```

调用链如下：

```text
sub.cpp::onConnection
    └─ PubSubClient::subscribe("news", onPublished)
         ├─ 保存 SubscribeCallback
         ├─ 拼接 "sub news\r\n"
         └─ PubSubClient::send
              └─ TcpConnection::send
                   ├─ socket 当前可写：直接 write
                   └─ 未写完：剩余字节进入 outputBuffer
```

`TcpConnection::send()` 不会阻塞等待内核发送缓冲区。能立即写多少就写多少；
剩余数据保存在 `outputBuffer` 中，并让 `Channel` 关注可写事件。以后 epoll 报告
socket 可写时，EventLoop 再继续发送。这就是示例中的发送侧背压处理。

### 3. hub 接收并登记订阅

字节到达 hub 后的调用链是：

```text
epoll: connection fd readable
    └─ Channel::handleEvent
         └─ TcpConnection::handleRead
              ├─ readv() 把字节追加到 inputBuffer
              └─ PubSubServer::onMessage(conn, inputBuffer, time)
                   └─ parseMessage
                        └─ doSubscribe(conn, "news")
```

`doSubscribe()` 同时更新两个方向：

```text
conn.context                         topics_["news"]
{"news", "sport"}                  Topic::audiences_
                                     {conn}
```

为什么需要两个索引？发布时需要从 topic 快速找到全部连接；断开时则需要从连接
快速找到它订阅过的全部 topic。只有一个方向的话，另一个操作就必须扫描全表。

## 数据流三：pub 发布，hub 广播，sub 显示

命令：

```bash
./build/examples/hub/pub 127.0.0.1:9999 news "hello"
```

### 1. pub 构造协议帧

单次发布者等待连接建立，在 connection callback 中执行：

```text
PubSubClient::publish("news", "hello")
```

它生成两行协议数据：

```text
pub news\r\n
hello\r\n
```

随后 `pub` 调用 `stop()` 发起优雅关闭。这里不会截断刚才的数据：
`TcpConnection` 会先处理已经写入或排队的内容，发送缓冲区清空后才关闭写端。

### 2. hub 解析并路由

hub 的 `TcpConnection` 把收到的字节追加到自己的 `inputBuffer`，然后调用
`PubSubServer::onMessage()`。`parseMessage()` 返回：

```text
cmd     = "pub"
topic   = "news"
content = "hello"
```

`onMessage()` 据此调用 `doPublish()`，后者找到 `topics_["news"]` 并进入
`Topic::publish()`：

1. 用 `"hello"` 更新 `content_`；
2. 记录本次发布时间 `lastPubTime_`；
3. 只拼接一次下行消息 `"pub news\r\nhello\r\n"`；
4. 遍历 `audiences_`，把同一个消息发送给每条订阅连接。

若 A 和 B 都订阅了 news，逻辑路由如下：

```text
pub process
    │  "pub news\r\nhello\r\n"
    ▼
hub TcpConnection (publisher)
    │
    ▼
parseMessage ──► topics_["news"].publish("hello")
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
       TcpConnection A       TcpConnection B
              │                     │
              ▼                     ▼
            sub A                 sub B
```

广播循环只负责调用各连接的 `send()`，不会等待慢客户端。每条连接有自己独立的
`outputBuffer`：A 的内核发送缓冲区堵塞时，不会阻止代码继续把消息交给 B。
不过，示例没有配置高水位淘汰策略；生产系统还需要限制长期慢客户端占用的
内存。

### 3. sub 解析并显示

订阅者收到消息后走与 hub 相同的网络/协议路径：

```text
epoll readable
    └─ TcpConnection::handleRead
         └─ PubSubClient::onMessage
              └─ parseMessage
                   └─ SubscribeCallback(topic, content, receiveTime)
                        └─ sub.cpp::onPublished
                             └─ println("news: hello")
```

`PubSubClient` 只保存一个 `SubscribeCallback`，但回调参数包含 topic，因此同一个
sub 进程可以用该回调处理它在一条连接上订阅的多个主题。

## 数据流四：TCP 半包与粘包

TCP 只保证字节顺序，不保证一次 `send()` 对应一次 `read()`。假设发送者发出：

```text
pub news\r\nhello\r\nsub sport\r\n
```

hub 可能分三次读到：

```text
read 1: "pub ne"
read 2: "ws\r\nhel"
read 3: "lo\r\nsub sport\r\n"
```

处理过程：

1. read 1 后找不到第一行 CRLF，返回 `kContinue`，Buffer 不消费任何字节。
2. read 2 后第一行完整，但 pub 的内容行没有 CRLF，仍返回 `kContinue`，之前
   的 header 也继续留在 Buffer。
3. read 3 后 pub 帧完整，解析器提交输出并只消费该帧。
4. `onMessage()` 看到 `kSuccess` 后继续循环，又从剩余 Buffer 解析出
   `sub sport`。这就是粘包处理。

因此，`parseMessage()` 遵守两个重要不变量：

- 返回 `kContinue` 或 `kError` 时，不修改 cmd/topic/content；
- 只有返回 `kSuccess` 时，才调用 `retrieveUntil()` 推进 Buffer 读指针。

收到语法非法的完整帧时返回 `kError`。协议没有长度字段或转义符来重新寻找
可靠边界，所以客户端和 hub 都选择 `shutdown()`，而不是冒险继续解释后续字节。

## 数据流五：retained message

`Topic` 不仅保存订阅者，还保存最近一次发布内容。数据流有两种顺序：

```text
先订阅、后发布：sub ──► audiences_ ──► publish 时正常广播
先发布、后订阅：publish 更新 content_ ──► add(conn) 立即补发
```

`lastPubTime_ == Timestamp::min()` 表示这个主题从未发布。此时新订阅者只加入
`audiences_`，不会收到空消息。一旦有过发布，`Topic::add()` 会调用
`makeMessage()`，把最近内容立即发送给新订阅者。

retained message 只存在 hub 内存中，不是消息队列：

- 每个主题只保留最后一条，而不是完整历史；
- hub 重启后全部消失；
- 它不记录每个订阅者是否已经消费。

## 数据流六：主动 unsub

客户端调用 `unsubscribe("news")` 时发送：

```text
unsub news\r\n
```

hub 解析后进入 `doUnsubscribe()`，同步更新两个索引：

```text
topics_["news"].audiences_.erase(conn)
conn.context.subscriptions.erase("news")
```

之后再发布 news 时，广播集合里已没有这条连接，因此它不会收到消息。连接本身
仍然存在，也可以继续接收其他已订阅主题或发送新命令。

## 数据流七：sub 异常退出或网络断开

关闭一个 sub 时，没有机会保证它先逐个发送 `unsub`。因此 hub 必须把“连接
断开”当作隐式取消该连接的所有订阅。

服务端调用链如下：

```text
peer close / read returns 0
    └─ TcpConnection::handleClose
         ├─ connectionCallback(conn disconnected)
         │    └─ PubSubServer::onConnection
         │         ├─ move conn.context subscriptions to local snapshot
         │         └─ for each topic: doUnsubscribe(conn, topic)
         │
         └─ closeCallback
              └─ TcpServer 从活动连接表移除 conn
```

这里 connection callback 先于 TcpServer 的 close callback 执行，所以业务层
清理订阅时 `TcpConnectionPtr` 仍然有效。

清理代码不能直接遍历 context 中的集合并调用 `doUnsubscribe()`，因为后者会
删除正在遍历的节点。正确做法是先移动出集合：

```cpp
ConnectionSubscription subscriptions =
    std::move(*connectionSubscriptions);
for (const auto& topic : subscriptions)
{
    doUnsubscribe(conn, topic);
}
```

局部变量 `subscriptions` 拥有原来的哈希节点；`doUnsubscribe()` 修改的是已经
移空的 context 集合，两者不再是同一个迭代目标。清理结束后，其他连接仍留在
各自 Topic 的 `audiences_` 中，hub 和其他 sub 都继续运行。

## 数据流八：utc_time 定时消息

`PubSubServer` 构造时注册：

```cpp
loop_->runEvery(1.0, [this] { timePublish(); });
```

定时器到期后，EventLoop 像处理 socket 事件一样执行 `timePublish()`。它以
`internal` 为来源调用同一个 `doPublish()`，因此后面的路由、retained message
更新和广播过程与普通 pub 完全相同，没有第二套特殊发送逻辑。

```text
TimerQueue / timerfd
    └─ EventLoop
         └─ timePublish()
              └─ doPublish("internal", "utc_time", now)
                   └─ Topic::publish
```

这也说明 EventLoop 不只处理网络 I/O，它统一串行化 socket、定时器和跨线程
投递任务。

## 线程、所有权与生命周期

### 线程归属

- hub 的 `topics_`、`Topic::audiences_` 和所有连接 context 都只在 hub 的
  EventLoop 线程访问，因此不需要锁。
- 普通 sub 和单次 pub 的 EventLoop 都运行在各自主线程。
- `pub ... -` 模式的主线程阻塞读取 stdin，EventLoop 在后台线程。此时
  `PubSubClient::publish()` 取得线程安全的连接快照，`TcpConnection::send()`
  再把消息投递到连接所属线程。

不要因为 `TcpConnection::send()` 支持跨线程，就推断所有业务对象都自动线程
安全。`topics_` 和订阅回调仍依赖 EventLoop 的线程归属约束。

### 对象所有权

```text
main stack
  ├─ EventLoop
  └─ PubSubServer / PubSubClient
       └─ TcpServer / TcpClient

TcpConnection shared ownership
  ├─ TcpServer 活动连接表或 TcpClient 当前连接
  ├─ Topic::audiences_（订阅期间）
  └─ 正在执行的回调 guard（回调期间）
```

回调 lambda 捕获 `this`，所以 `PubSubServer`/`PubSubClient` 必须比已注册回调
活得更久。示例通过对象声明顺序和 `loop.loop()` 的作用域满足这一点：业务对象
在进入 loop 前构造，在 loop 返回后才析构，而 EventLoop 最后析构。

### 一条消息发生了多少次数据转换

以 `"hello"` 为例：

1. pub 把 topic/content 拼成 wire string；
2. 客户端 TcpConnection 把字节交给 socket，必要时复制到 outputBuffer；
3. hub 从 socket 读入 inputBuffer；
4. `parseMessage()` 把视图提交为 cmd/topic/content 字符串；
5. `Topic` 保存 content，并只构造一次下行 wire string；
6. 每个订阅 TcpConnection 分别发送，慢连接可能复制到自己的 outputBuffer；
7. sub 读入 Buffer、再次解析，最终把字符串交给显示回调。

这个设计优先展示清晰的分层和正确的异步 I/O。若追求极致吞吐，可以进一步
研究长度前缀二进制协议、引用计数共享广播消息、批量写和高水位限流。

## 构建

项目默认构建 examples：

```bash
cmake -S . -B build
cmake --build build --target hub pub sub -j
```

程序位于 `build/examples/hub/`。

## 运行完整示例

打开四个终端。

终端 1，启动消息中心：

```bash
./build/examples/hub/hub 9999
```

终端 2，订阅 `mytopic`：

```bash
./build/examples/hub/sub 127.0.0.1:9999 mytopic
```

终端 3，同时订阅 `mytopic` 和 `court`：

```bash
./build/examples/hub/sub 127.0.0.1:9999 mytopic court
```

终端 4，发布两条消息：

```bash
./build/examples/hub/pub 127.0.0.1:9999 mytopic "Hello world."
./build/examples/hub/pub 127.0.0.1:9999 court "13:11"
```

两个订阅者都会收到 `mytopic`，只有终端 3 会收到 `court`。

hub 每秒还会内部发布一次 `utc_time`：

```bash
./build/examples/hub/sub 127.0.0.1:9999 utc_time
```

## 连续发布

内容参数使用 `-` 时，`pub` 会逐行读取 stdin：

```bash
./build/examples/hub/pub 127.0.0.1:9999 mytopic -
first message
second message
```

输入 EOF（终端通常为 Ctrl-D）后发布者优雅断开。

## 为教学而保留的简化

- topic 和 content 不支持 CRLF 转义，也没有帧长度上限。
- 客户端没有保存订阅集合，因此断线重连后不会自动重新订阅。
- `pub`/`sub` 的命令行解析只覆盖 IPv4 风格的 `host:port`。
- hub 不持久化 retained message，重启后历史内容会消失。
