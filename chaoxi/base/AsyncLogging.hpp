#pragma once

///
/// @file AsyncLogging.hpp
/// @brief AsyncLogging —— 双缓冲异步日志：前端短临界区写入，后端批量落盘
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║  架构：双缓冲 + 前端生产者/后端消费者                               ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║   前端（N 个用户线程）               后端（1 个日志线程）             ║
/// ║   ╭──────────────────╮              ╭──────────────────╮             ║
/// ║   │ append(msg)       │              │ threadFunc()     │             ║
/// ║   │  ├─ 获取互斥锁     │              │  后端循环         │             ║
/// ║   │  ├─ 写当前缓冲     │              │    ├─ 等待通知     │             ║
/// ║   │  │   缓冲写满      │              │    ├─ 交换缓冲队列 ║
/// ║   │  │   → 加入队列    │ ═══════════▶│    ├─ 写入日志文件 ║
/// ║   │  │   并通知后端    │   队列      │    ├─ 刷新文件     ║
/// ║   │  └─ 释放互斥锁     │              │    └─ 回收缓冲     ║
/// ║   ╰──────────────────╯              ╰──────────────────╯             ║
/// ║                                                                      ║
/// ║  双缓冲流动：                                                         ║
/// ║  ────────────                                                         ║
/// ║                                                                      ║
/// ║  初始状态:                                                            ║
/// ║    前端持有：currentBuffer_、nextBuffer_（两个空缓冲）               ║
/// ║    后端持有：newBuffer1、newBuffer2（两个空缓冲）                    ║
/// ║                                                                      ║
/// ║  写入过程:                                                            ║
/// ║    ① 前端往 currentBuffer_ 写                                         ║
/// ║    ② currentBuffer_ 写满：加入 buffers_，nextBuffer_ →               ║
/// ║      currentBuffer_                                                   ║
/// ║    ③ 后端与 buffers_ 交换待写数据，写入 LogFile                      ║
/// ║    ④ 后端归还写完的空缓冲：newBuffer1 → nextBuffer_（给前端）        ║
/// ║                                                                      ║
/// ║  积压保护:                                                            ║
/// ║    若 buffersToWrite.size() > 25，说明后端过慢，丢弃多余缓冲          ║
/// ║    只保留前 2 个, 其余丢弃并打印提示                                   ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝
///

#include "chaoxi/base/LogStream.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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
    struct Statistics
    {
        std::uint64_t acceptedMessages{};
        std::uint64_t acceptedBytes{};
        std::uint64_t writtenMessages{};
        std::uint64_t writtenBytes{};
        std::uint64_t droppedMessages{};
        std::uint64_t droppedBytes{};
        std::uint64_t droppedBuffers{};
    };

    static constexpr std::size_t kDefaultBufferSize = 4UL * 1024 * 1024;  // 4 MiB
    AsyncLogging(std::string basename,
                 std::size_t rollSize,
                 int flushInterval = 3);
    ~AsyncLogging();

    AsyncLogging(const AsyncLogging&) = delete;
    AsyncLogging& operator=(const AsyncLogging&) = delete;
    AsyncLogging(AsyncLogging&&) = delete;
    AsyncLogging& operator=(AsyncLogging&&) = delete;

    ///
    /// 前端写入（用户线程调用, 线程安全）
    ///
    /// 快速路径：currentBuffer_ 有空间时直接追加（最小锁粒度）。
    /// 慢速路径：currentBuffer_ 写满后加入队列，换用 nextBuffer_ 并通知后端。
    ///
    void append(std::string_view msg);

    void start();

    /// 停止接收新消息，排空所有已接收缓冲，并等待后端线程退出后再返回。
    void stop();

    /// 获取累计计数快照。written 表示数据已交给 LogFile；若要求持久化到
    /// 存储设备，调用方还需要显式同步文件。
    [[nodiscard]] Statistics statistics() const noexcept;

private:
    static constexpr std::size_t kBufferToWriteMaxSize = 25;
    static constexpr std::size_t kBufferToWriteDefaultCap = 16;

    ///
    /// 后端线程主循环：等待 → 收集 → 写入 LogFile → 回收缓冲。
    ///
    /// 循环中的缓冲流转：
    ///   newBuffer1 / newBuffer2        ← 后端持有的两个空缓冲
    ///   currentBuffer_ / nextBuffer_   ← 前端持有的两个缓冲
    ///   buffers_                       ← 前端→后端的待写队列
    ///   buffersToWrite                 ← 后端本次要写入的缓冲
    ///
    ///  每次循环:
    ///    ① currentBuffer_ 加入待写队列, newBuffer1 → currentBuffer_（给前端）
    ///    ② buffers_ 与 buffersToWrite 交换（后端拿到所有待写数据）
    ///    ③ nextBuffer_ 如果为空, newBuffer2 → nextBuffer_（补给前端）
    ///    ④ 写入 LogFile
    ///    ⑤ 将用完的空缓冲回收到 newBuffer1/newBuffer2
    ///
    void threadFunc();

    using Buffer = chaoxi::detail::FixedBuffer<chaoxi::detail::kLargeBuffer>;
    using BufferPtr = std::unique_ptr<Buffer>;

    struct PendingBuffer
    {
        BufferPtr buffer;
        std::uint64_t messages{};
    };

    using BufferVector = std::vector<PendingBuffer>;

    const int flushInterval_;
    std::atomic<bool> running_{false};
    const std::string basename_;
    const std::size_t rollSize_;

    std::jthread thread_;
    std::latch latch_{1};

    mutable std::mutex mutex_;
    std::condition_variable cond_;

    BufferPtr currentBuffer_;  // 当前前端写入的缓冲
    std::uint64_t currentBufferMessages_{};
    BufferPtr nextBuffer_;  // 预备缓冲，当前缓冲写满后立即可用。
    BufferVector buffers_;  // 已写满、待后端写入的缓冲队列

    std::uint64_t acceptedMessages_{};
    std::uint64_t acceptedBytes_{};
    std::atomic<std::uint64_t> writtenMessages_{};
    std::atomic<std::uint64_t> writtenBytes_{};
    std::atomic<std::uint64_t> droppedMessages_{};
    std::atomic<std::uint64_t> droppedBytes_{};
    std::atomic<std::uint64_t> droppedBuffers_{};
};
}  // namespace chaoxi
