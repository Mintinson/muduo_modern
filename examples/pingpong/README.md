# PingPong Benchmark Suite

TCP 吞吐量 + 延迟性能测试套件，支持一键运行、数据记录和可视化。

## Quick Start

```bash
# 1. Build
cd build && ninja pingpong_server pingpong_client pingpong_bench

# 2. Quick test (few parameters)
cd ../examples/pingpong
python3 run_benchmark.py --quick

# 3. Full test
python3 run_benchmark.py

# 4. Visualize results (requires streamlit, pandas and plotly)
source ~/miniconda3/etc/profile.d/conda.sh && conda activate dreamvla
streamlit run interactive_plot.py
```

## 文件说明

| 文件 | 用途 |
|------|------|
| `server.cpp` | Echo server（TcpServer） |
| `client.cpp` | Pingpong client（TcpClient + EventLoopThreadPool） |
| `bench.cpp` | 专业 benchmark（throughput / latency 两种模式） |
| `run_benchmark.py` | 编排脚本：自动检测 CPU，遍历参数矩阵，收集数据 |
| `interactive_plot.py` | 交互式可视化不同 benchmark 结果对比 |
| `results/` | 存放 benchmark CSV 目录 |

## bench.cpp 模式

### throughput 模式
```
./pingpong_bench -m throughput -t 4 -s 100 -b 1024 -d 10
```
- `-t`: 客户端线程数（server 内部固定 4 线程）
- `-s`: 并发连接数
- `-b`: 消息大小（bytes）
- `-d`: 测试时长（seconds）

### latency 模式
```
./pingpong_bench -m latency -n 100 -a 1 -w 100
```
- `-n`: socketpair 对数
- `-a`: 同时活跃的管道数
- `-w`: 每次迭代的写入次数

## Python 脚本

### run_benchmark.py
```bash
python3 run_benchmark.py [--quick] [--time 10] [--server ./srv] [--client ./cli] [--tags libevent]
```
- `--quick`: 减少参数矩阵（快速测试）
- `--time N`: 每次测试 N 秒
- `--server/--client`: 指定自定义二进制（用于对比测试）
- `--tags`: 标记实验名称

### interactive_plot.py
```bash
streamlit run interactive_plot.py
```
在弹出的浏览器界面中可以点击改变 Session 和 BlockSize 数，方便对比不同框架的对比

## 与其他库对比

```bash
# 运行 chaoxi benchmark
python3 run_benchmark.py --tags chaoxi

# 对比其他库（替换 --server 和 --client）
python3 run_benchmark.py --server ./libevent_server --client ./libevent_client --tags libevent
python3 run_benchmark.py --server ./asio_server --client ./asio_client --tags asio
```

## benchmark 结果表示

