运行方法，在同一台机器的两个命令行窗口分别运行：

```bash
 ./netty_discard_server
```

```bash
./netty_discard_client 127.0.0.1 256
```

第一个窗口显示吞吐量：

```text
10.321 MiB/s 19.471 Ki Msgs/s 542.81 bytes per msg
27.376 MiB/s 51.712 Ki Msgs/s 542.09 bytes per msg
27.579 MiB/s 51.763 Ki Msgs/s 545.58 bytes per msg
27.522 MiB/s 48.017 Ki Msgs/s 586.93 bytes per msg
26.174 MiB/s 49.698 Ki Msgs/s 539.31 bytes per msg
26.965 MiB/s 50.648 Ki Msgs/s 545.18 bytes per msg
26.081 MiB/s 47.938 Ki Msgs/s 557.11 bytes per msg
```

改变第二个命令的最后一个参数（上面的256），可以观察不同的消息大小对吞吐量的影响。

练习1：把二者的关系绘制成函数曲线，看看有什么规律，想想为什么。(使用 python + matplotlib 绘制)

## 练习1 解答

### 使用方法

本项目提供了两个脚本：

1. **`discard_benchmark.py`** — 基准测试运行器，自动测试多个消息大小并采集吞吐量数据。
2. **`plot_results.py`** — 结果分析绘图工具，从 CSV 生成分析报告和四面板图。

### 运行基准测试

```bash
# 单次测试（快速验证）
python3 discard_benchmark.py \
    --impl "muduo_modern" \
        ../../../build/examples/netty/discard/netty_discard_server \
        ../../../build/examples/netty/discard/netty_discard_client \
    --duration 12 \
    --sizes "16,32,64,128,256,512,1024,2048,4096,8192,16384"

# 对比测试（新旧实现）
python3 discard_benchmark.py \
    --impl "muduo_modern" \
        ../../../build/examples/netty/discard/netty_discard_server \
        ../../../build/examples/netty/discard/netty_discard_client \
    --impl "muduo_old" \
        /path/to/muduo/build/release-cpp11/bin/netty_discard_server \
        /path/to/muduo/build/release-cpp11/bin/netty_discard_client \
    --duration 12 \
    --save discard_benchmark_comparison.png \
    --csv discard_comparison.csv
```

### 测试结果

在本地回环（127.0.0.1）上测试，单线程（0 个额外 IO 线程），每次测试运行 12 秒。

| Msg Size (B) | muduo_modern (MiB/s) | muduo_old (MiB/s) | 差异 |
|:---:|:---:|:---:|:---:|
| 16 | 5.031 | 4.844 | +3.86% |
| 32 | 9.885 | 9.614 | +2.82% |
| 64 | 19.043 | 18.695 | +1.86% |
| 128 | 40.082 | 38.950 | +2.91% |
| 256 | 75.152 | 75.424 | -0.36% |
| 512 | 155.490 | 147.202 | +5.63% |
| 1024 | 303.951 | 297.374 | +2.21% |
| 2048 | 613.736 | 586.106 | +4.71% |
| 4096 | 1162.272 | 1095.914 | +6.06% |
| 8192 | 2061.521 | 2003.975 | +2.87% |
| 16384 | 3550.879 | 3436.408 | +3.33% |

**加权平均提升：+3.66%**

### 规律分析

从数据和曲线中可以观察到以下规律：

1. **吞吐量随消息大小单调递增，且近似线性关系**（在对数-对数坐标下斜率接近 1）。
   - 原因是：相同数量的消息，每次发送的消息越大，有效数据占比越高，TCP 及协议栈的固定开销（系统调用、中断、上下文切换）被摊薄。

2. **消息速率（Ki Msgs/s）基本恒定（~300K msg/s）**，在大消息时略有下降。
   - 这反映了服务器的**每消息处理能力极限**：每次 `onMessage()` 回调处理 + `buf.retrieveAll()` 的固定开销大约为 3-4μs。
   - 当消息大到一定程度（> 4KB），单条消息的处理开始受内存带宽限制，速率开始下降。

3. **小消息（16-128B）时吞吐量较低**，因为 TCP 协议开销占比大：
   - 每个 TCP segment 有 20B TCP header + 20B IP header + 14B Ethernet header → 约 54B 协议开销
   - 16B 应用数据：实际线路有效利用率 = 16/(16+54) ≈ 23%
   - 256B 数据：利用率 = 256/(256+54) ≈ 82%

4. **本地回环（loopback）测试特点**：不需要经过物理网卡，数据在内存中通过内核直接转发。
   - 16384B 消息达到 ~3.5 GiB/s ≈ 28 Gbps，远超千兆网卡理论极限（1 Gbps），说明瓶颈在内核协议栈处理能力而非带宽。
   - 实际跨网络测试时，吞吐量会受网卡带宽和网络延迟的限制。

5. **muduo_modern 比 muduo_old 平均快 ~3.7%**，主要受益于：
   - 使用 C++23 `std::atomic`（替代 `muduo::AtomicInt64`）带来更优的内存序和编译器优化
   - 使用 `std::chrono` 替代自定义时间戳类
   - 编译器版本和标准库实现的差异