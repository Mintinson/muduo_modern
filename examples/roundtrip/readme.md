# Roundtrip — 网络延迟与时钟偏差测量

## 原理

Roundtrip 通过消息回声（ping-pong）的方式测量两台主机之间的**网络往返延迟（RTT）**和**时钟偏差（clock skew）**，其原理与 NTP 时间同步协议中的 Cristian 算法一致。

### 消息格式

每条消息固定 16 字节，包含两个 `int64_t` 时间戳字段：

```
┌──────────────────────────┬──────────────────────────┐
│    message[0]: send_time │  message[1]: recv_time   │
│         (μs)             │         (μs)             │
└──────────────────────────┴──────────────────────────┘
```

### 工作流程

```
客户端时钟: T₁            T₂                          T₃
            │             │                            │
客户端      ├─── send ───→│                            │
            │             ├─── server recv @ T_server ─┤
服务端      │             │                            │
            │             │←──────── echo ─────────────┤
            │             │                            │
            ▼             ▼                            ▼
时间线 ──────────────────────────────────────────────────→
```

1. **客户端**每 200ms 向服务端发送当前本地时间 `send_time = T₁`。
2. **服务端**收到后，记录自己的接收时间 `recv_time = T_server`（精度微秒），然后将 `[send_time, recv_time]` 原样发回客户端。
3. **客户端**收到回声后，记录当前本地时间 `back = T₃`，然后计算：

   ```
   RTT = back - send_time          // 往返延迟
   RTT 单向 = RTT / 2              // 假设路径对称
   mine = (T₃ + T₁) / 2            // 客户端估算的服务端接收时刻
   clock error = T_server - mine   // 正数 = 服务端时钟领先
   ```

### 为什么这样能算时钟偏差？

如果假设**网络路径对称**（上行延迟 = 下行延迟），那么服务端真正收到消息的时刻，在客户端时间轴上是 `T₁ + RTT/2 = (T₁ + T₃)/2`。将这个估计值 `mine` 与服务端实际记录的时间 `T_server` 相比，差值即为两台机器之间的时钟偏差。

### TCP vs UDP 版本

`roundtrip.cpp` 使用 TCP，`roundtrip_udp.cpp` 使用 UDP，两者算法完全相同。

> **输出格式差异**：TCP 版将时间戳差除以 1000.0 后输出（浮点 μs），并标注 `us`；UDP 版直接输出原始整数微秒，**不带单位标签**。这是历史原因，两种格式都能用。

| 特性 | TCP | UDP |
|---|---|---|
| 传输层 | 可靠字节流（有重传、流量控制） | 不可靠数据报 |
| 代码复杂度 | 需 while 循环处理粘包（注释已说明） | 消息天然有边界 |
| 延迟影响因素 | 内核 TCP 处理 + Nagle（已设 TCP_NODELAY） | 更少协议栈开销 |
| 输出示例 | `round trip 204.118us, clock error 25.019us` | `round trip 201 clock error 22` |
| 适用场景 | 测试可靠传输下的延迟 | 测试原始网络延迟 |

TCP 版本用 `while` 而非 `if` 读取的重要原因是：**TCP 是字节流协议，不保留消息边界**。客户端每秒发 5 个消息，服务端一次 `read` 可能读到多个完整的消息，所以需要 `while (buffer.readableBytes() >= frameLen)` 循环把所有完整消息处理完。

## 使用方式

### 编译

```bash
cd build && ninja roundtrip roundtrip_udp
```

### 运行

两个终端窗口，或一台机器当服务端、另一台当客户端。

**终端 1（服务端）：**

```bash
./roundtrip -s 8888        # TCP 版，监听 8888 端口
./roundtrip_udp -s 8888    # UDP 版
```

**终端 2（客户端）：**

```bash
./roundtrip 127.0.0.1 8888       # TCP 版，连接本机
./roundtrip_udp 127.0.0.1 8888   # UDP 版
```

也可以用远程 IP 跨机器测试：

```bash
./roundtrip 192.168.1.100 8888   # 连接远程服务器
```

## 预期结果

### 本地回环（127.0.0.1）

```
round trip 204.118us, clock error 25.019us
round trip 200.407us, clock error 34.186us
round trip 192.054us, clock error 53.196us
round trip 62.796us, clock error 7.512us
round trip 239.704us, clock error 11.169us
round trip 176.054us, clock error -36.971us
round trip 199.947us, clock error 12.858us
```

- **RTT 通常 60–300μs**，因为数据不经过物理网卡，仅在内核中走一遭。波动来自 CPU 调度抖动和 cache miss。
- **时钟偏差接近 0**（典型 ±50μs），因为客户端和服务端运行在同一台机器的同一系统时钟上。
- **时钟偏差接近 0**，因为客户端和服务端运行在同一台机器的同一系统时钟上。

### 局域网（同网段机器）

```
INFO  round trip 0.412ms, clock error 0.025ms
INFO  round trip 0.398ms, clock error 0.031ms
```

- **RTT 通常 0.3–1ms**，取决于交换机和网卡延迟。
- **时钟偏差**取决于两台机器的 NTP 同步精度，通常在亚毫秒到几毫秒。

### 跨互联网

```
INFO  round trip 45.327ms, clock error 2.134ms
INFO  round trip 43.891ms, clock error 2.201ms
```

- **RTT 通常在几十到几百毫秒**，取决于物理距离和路由跳数。
- **延迟抖动（jitter）**明显增加，每条消息的 RTT 可能波动几个毫秒甚至更大。
- **时钟偏差**的波动也相应增大，因为非对称路径（上下行路径不同或拥塞程度不同）会破坏 Cristian 算法的对称假设。

## 对比参考

与 `examples/pingpong/` 的区别：

- **pingpong**：测量**吞吐量**（高并发下每秒能处理多少消息/数据量），关注服务端承载力。
- **roundtrip**：测量**延迟**（单个消息往返一次的时间），关注网络质量和时钟同步精度。

两者测量的是网络性能的两个正交维度。
