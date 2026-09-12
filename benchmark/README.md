# Chaoxi 性能基线

默认运行器会测量并汇总两套基准：

- `base`：LogStream、Logging、LogFile、AsyncLogging、BlockingQueue，以及阻塞队列与有界队列的对比；
- `net`：Buffer、HTTP 解析与序列化、EventLoop 跨线程投递、TimerQueue 和 TCP 回环往返；
- 端到端场景：socketpair 延迟、TCP 吞吐和连接风暴。

从仓库根目录运行完整的 Release/NDEBUG 基线：

```sh
python3 benchmark/run_baseline.py
```

需要快速冒烟或只调查一个套件时，可以使用：

```sh
python3 benchmark/run_baseline.py --quick
python3 benchmark/run_baseline.py --suite base
python3 benchmark/run_baseline.py --suite net
python3 benchmark/run_baseline.py --suite base --cpus 0-7
```

`--cpus` 接受 Linux `taskset` 的 CPU 列表语法，包括范围和逗号分组。并发基准必须分配足够的物理核心；如果把整个套件限制到一个逻辑 CPU，多线程扩展性数据将失去意义。旧参数 `--cpu N` 仅作为兼容写法保留。

每个计时场景都会先预热，默认重复五次。结果写入 `benchmark/results/<时间戳>/`：

- `metadata.json`：提交版本、工作区状态、编译器、编译参数、CPU、内核、事件后端、LTO/PGO 状态和诊断工具可用性；
- `google-benchmark/{base,net}/*.json`：未经修改的 Google Benchmark 原始输出；
- `base-summary.json` 和 `net-summary.json`：分别汇总两套基准；
- `google-benchmark-{raw,summary}.csv`：合并后的微基准原始数据与汇总；
- `standalone-{raw,summary}.csv`：不使用 Google Benchmark 的 LogFile 数据；
- `resource-usage.csv`：CPU 时间、上下文切换、缺页和最大常驻内存；
- `e2e-{raw,summary}.csv`：端到端指标分布；
- `diagnostics/{base,net}/`：独立运行的 strace 和 heaptrack 诊断；
- `summary.json`：合并后的机器可读报告。

产生日志的基准使用私有临时目录，运行结束时自动删除。生成的数据文件不会污染 `/tmp`，也不会放进结果目录。

## 指标怎样理解

- `real_time`：墙钟耗时。多线程吞吐必须以它为准，因为 `cpu_time` 会累计多个线程的 CPU 时间。
- `items_per_second`、`bytes_per_second`：单位墙钟时间完成的业务操作数和字节数，越高越好；必须同时确认校验计数为零错误。
- `p50`：典型样本；`p99` 和 `p99.9`：尾部慢请求；`max` 很容易受调度中断影响，通常不宜单独作为回归门槛。
- `CV`（变异系数）：标准差除以均值。微基准低于 2% 通常很稳，2%–5% 可用于趋势判断，超过 5% 应增加重复次数、隔离核心或调查系统噪声；这些范围是工程经验，不是跨机器的硬标准。
- `cpu_percent`：所有线程 CPU 时间之和除以墙钟时间，因此多线程程序可以超过 100%。
- `voluntary_context_switches`：线程因锁、条件变量或 I/O 主动睡眠；`involuntary_context_switches`：被调度器抢占。两者必须结合工作负载和吞吐解读。
- `minor_page_faults`：通常只是建立内存页映射，不代表磁盘 I/O；`major_page_faults` 才表示需要存储设备参与。
- `max_rss_kib`：进程峰值常驻内存，只反映该次可执行文件整体，不能直接归因于其中某一个测试用例。
- `strace`：当前只统计独立诊断运行的系统调用。它会显著扰动时序，所以不混入正式样本。
- `heaptrack`：报告独立诊断运行的动态分配次数和调用栈。CPU 亲和性工具在分析器外层启动，避免把 `taskset` 自身误当成被测程序。启动代码与基准框架自身的分配仍包含在内，应该关注热循环是否随迭代数线性增长，而不是追求绝对为零。

## 各个 base 基准的测量边界

- `LogStream` 和 `Logging` 测量前端格式化与日志对象构造，不包含真实文件 I/O。它们适合比较格式化路径，不代表完整日志系统吞吐。
- `AsyncLogging` 从所有生产者同时开始计时，到 `stop()` 排空并等待后端退出为止；使用墙钟总吞吐，并校验接收消息数、后端写入消息数和关闭后的实际文件字节数。`written` 表示已交给 LogFile，文件大小表示数据对文件系统可见；二者都不等价于物理介质持久化。
- `BlockingQueue` 吞吐测试计时到最后一条业务消息被消费，停机毒药丸和线程销毁不计时。延迟分布从入队前时间戳量到出队后时间戳，因此测到的是“队列驻留时间 + 锁竞争 + 调度延迟”，不是一次互斥锁操作的纯耗时。
- `BlockingQueue` 延迟测试让生产者突发写满队列。这是开放式突发负载：消费者跟不上时，后面的消息会自然累积毫秒级排队延迟。不要把这些分位数误读成无积压状态下的单操作延迟。
- `QueueComparison` 的有界队列在满或空时忙等并让出 CPU。更高吞吐可能以更多 CPU 占用为代价，比较时必须同时看资源数据。
- `LogFile` 分离 `tmpfs`、`page-cache` 和 `fsync`。`page-cache` 只刷新 C 库缓冲，不强制写入设备；`fsync` 在每批一百万条日志结束时做一次同步，所以它表示批量同步的摊销成本，不是逐条同步成本。`metadata.json` 会记录两类目录所在的文件系统。

资源统计以“整个基准可执行文件”为单位。例如一次 LogFile 进程依次运行五个场景，因此该行 CPU 和上下文切换不能精确拆给某一个场景。需要逐场景资源归因时，应给可执行文件增加过滤参数并分别启动进程。

不同元数据配置不能当作同一条基线直接比较。至少要保持编译器、优化参数、LTO/PGO、CPU、内核、事件后端和 CPU 亲和性一致；CPU 调频开启时还应把频率波动视为噪声来源。
