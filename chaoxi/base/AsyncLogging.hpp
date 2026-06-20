#pragma once

///
/// @file AsyncLogging.hpp
/// @brief AsyncLogging —— 双缓冲异步日志：前端无锁写，后端批量落盘
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║  架构：双缓冲 (double-buffering) + 前端/后端（producer/consumer）    ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║   前端（N 个用户线程）               后端（1 个日志线程）             ║
/// ║   ╭──────────────────╮              ╭──────────────────╮             ║
/// ║   │ append(msg)       │              │ threadFunc()     │             ║
/// ║   │  ├─ lock(mutex_)  │              │  while(running_) │             ║
/// ║   │  ├─ currentBuffer_│              │    ├─ wait(cond) │             ║
/// ║   │  │   .append(msg) │              │    ├─ swap buffers_           ║
/// ║   │  │   满 → push     │   buffers_  │    ├─ write to LogFile        ║
/// ║   │  │   buffers_,     │ ═══════════▶│    ├─ flush                   ║
/// ║   │  │   notify(cond)  │   队列      │    ├─ recycle buffers         ║
/// ║   │  └─ unlock         │              │    └─ loop                   ║
/// ║   ╰──────────────────╯              ╰──────────────────╯             ║
/// ║                                                                      ║
/// ║  双缓冲流动：                                                         ║
/// ║  ────────────                                                         ║
/// ║                                                                      ║
/// ║  初始状态:                                                            ║
/// ║    前端持有: currentBuffer_, nextBuffer_ (2 个空 buffer)             ║
/// ║    后端持有: newBuffer1, newBuffer2 (2 个空 buffer)                  ║
/// ║                                                                      ║
/// ║  写入过程:                                                            ║
/// ║    ① 前端往 currentBuffer_ 写                                         ║
/// ║    ② currentBuffer_ 写满: push 到 buffers_ 队列, nextBuffer_ →       ║
/// ║      currentBuffer_                                                   ║
/// ║    ③ 后端从 buffers_ 队列 swap 出待写数据, 写入 LogFile              ║
/// ║    ④ 后端把写完的空 buffer 归还: newBuffer1 → nextBuffer_ (给前端)   ║
/// ║                                                                      ║
/// ║  积压保护:                                                            ║
/// ║    如果 buffersToWrite.size() > 25, 说明后端写得太慢, 丢弃多余 buffer ║
/// ║    只保留前 2 个, 其余丢弃并打印提示                                   ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝
///

#include "chaoxi/base/LogStream.hpp"

#include <atomic>
#include <condition_variable>
#include <latch>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace chaoxi
{

class AsyncLogging
{
public:
    static constexpr std::size_t kDefaultBufferSize = 4 * 1024 * 1024;  // 4MB
    AsyncLogging(std::string basename, off_t rollSize, int flushInterval = 3);
    ~AsyncLogging();

    AsyncLogging(const AsyncLogging&) = delete;
    AsyncLogging& operator=(const AsyncLogging&) = delete;
    AsyncLogging(AsyncLogging&&) = delete;
    AsyncLogging& operator=(AsyncLogging&&) = delete;

    ///
    /// 前端写入（用户线程调用, 线程安全）
    ///
    /// 快速路径: currentBuffer_ 有空间 → 直接 append（粒度最小锁）
    /// 慢速路径: currentBuffer_ 满了 → push 到队列, 换 nextBuffer_, 通知后端
    ///
    void append(std::string_view msg);

    void start();
    void stop();

private:
    ///
    /// 后端线程主循环: 等待 → 收集 → 写入 LogFile → 回收 buffer
    ///
    /// 循环中的 buffer 流转:
    ///   newBuffer1 / newBuffer2        ← 后端持有的两个空 buffer
    ///   currentBuffer_ / nextBuffer_   ← 前端持有的两个 buffer
    ///   buffers_                       ← 前端→后端的待写队列
    ///   buffersToWrite                 ← 后端本次要写入的 buffer
    ///
    ///  每次循环:
    ///    ① currentBuffer_ 加入待写队列, newBuffer1 → currentBuffer_（给前端）
    ///    ② buffers_ 与 buffersToWrite 交换（后端拿到所有待写数据）
    ///    ③ nextBuffer_ 如果为空, newBuffer2 → nextBuffer_（补给前端）
    ///    ④ 写入 LogFile
    ///    ⑤ 回收用完的空 buffer 到 newBuffer1/newBuffer2
    ///
    void threadFunc();
    
    using Buffer = chaoxi::detail::FixedBuffer<chaoxi::detail::kLargeBuffer>;
    using BufferPtr = std::unique_ptr<Buffer>;
    using BufferVector = std::vector<BufferPtr>;

    const int flushInterval_;
    std::atomic<bool> running_{false};
    const std::string basename_;
    const off_t rollSize_;

    std::jthread thread_;
    std::latch latch_{1};

    std::mutex mutex_;
    std::condition_variable cond_;

    BufferPtr currentBuffer_;  // 当前前端写入的缓冲
    BufferPtr nextBuffer_;     // 预备缓冲，前端 current 写满后立即可用
    BufferVector buffers_;     // 已写满、待后端写入的缓冲队列
};
}  // namespace chaoxi
