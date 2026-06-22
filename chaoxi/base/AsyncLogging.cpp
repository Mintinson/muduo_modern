

#include "chaoxi/base/AsyncLogging.hpp"

#include "chaoxi/base/LogFile.hpp"
#include "chaoxi/base/Timestamp.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace chaoxi
{

AsyncLogging::AsyncLogging(std::string basename,
                           std::size_t rollSize,
                           int flushInterval)
    : flushInterval_(flushInterval)
    , basename_(std::move(basename))
    , rollSize_(rollSize)
    , currentBuffer_(std::make_unique<Buffer>())  // 前端当前写入的目标
    , nextBuffer_(std::make_unique<Buffer>())     // 前端备用 buffer
{
    currentBuffer_->bzero();
    nextBuffer_->bzero();
    buffers_.reserve(16);  // 预分配队列空间
}

AsyncLogging::~AsyncLogging()
{
    if (running_.load(std::memory_order_acquire))
    {
        stop();
    }
}

void AsyncLogging::append(std::string_view msg)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (currentBuffer_->avail() > msg.size())  // 最常见情况：空间够
    {
        // ── 快速路径: 当前 buffer 有足够空间, 直接写入 ──
        currentBuffer_->append(msg);
    }
    else
    {
        // ── 慢速路径: 当前 buffer 满了 ──
        // ① 把满的 currentBuffer_ 移动到待写队列
        buffers_.push_back(std::move(currentBuffer_));

        // ② 从前端备用 buffer 中取一个新的（如果还有的话）
        if (nextBuffer_) [[likely]]
        {
            currentBuffer_ = std::move(nextBuffer_);
        }
        else
        {
            // 极少发生: 前端写太快, 后端还没来得及归还 nextBuffer_
            currentBuffer_ = std::make_unique<Buffer>();
        }

        // ③ 写入新 buffer
        currentBuffer_->append(msg);
        // ④ 唤醒后端线程来消费
        cond_.notify_one();
    }
}

void AsyncLogging::start()
{
    running_.store(true, std::memory_order_release);
    thread_ = std::jthread([this] { threadFunc(); });
    // latch 确保 start() 返回时后端线程已经进入 threadFunc 并完成初始化
    latch_.wait();
}

void AsyncLogging::stop()
{
    running_.store(false, std::memory_order_release);
    cond_.notify_one();  // 唤醒正在 wait 的后端线程让它退出循环
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void AsyncLogging::threadFunc()
{
    latch_.count_down();  // 通知 start() 线程已启动

    // 打开日志文件（不需要线程安全, 只在后端线程访问）
    LogFile output(basename_, rollSize_, false);

    // 后端空 buffer 池: 用于补给前端
    auto newBuffer1 = std::make_unique<Buffer>();  // 后端预备的两个空闲缓冲
    auto newBuffer2 = std::make_unique<Buffer>();
    newBuffer1->bzero();
    newBuffer2->bzero();

    BufferVector buffersToWrite;  // 后端本次要写入文件的缓冲集合
    // BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    while (running_.load(std::memory_order_acquire))
    {
        assert(newBuffer1 && newBuffer1->length() == 0);
        assert(newBuffer2 && newBuffer2->length() == 0);
        assert(buffersToWrite.empty());

        {
            std::unique_lock<std::mutex> lock(mutex_);

            // 等待: 要么有数据到来, 要么 flushInterval_ 秒超时
            if (buffers_.empty())
            {
                cond_.wait_for(lock, std::chrono::seconds(flushInterval_));
            }

            // ── 三步交换 ──
            // ① 把当前正在写的 currentBuffer_ 也加入队列
            //    (即使它没满, 也要定期刷盘——这就是 flushInterval_ 的作用)
            buffers_.push_back(std::move(currentBuffer_));

            // ② newBuffer1 补给前端作为新的 currentBuffer_
            currentBuffer_ = std::move(newBuffer1);

            // ③ 交换: 后端拿到所有待写 buffer, buffers_ 变为空
            buffersToWrite.swap(buffers_);

            // 补给前端的备用 buffer
            if (!nextBuffer_)
            {
                nextBuffer_ = std::move(newBuffer2);
            }
        }

        assert(!buffersToWrite.empty());

        // ── 积压保护 ──
        // 如果待写的 buffer 超过 25 个, 说明后端跟不上前端的速度
        // 丢弃多余的, 只保留前 2 个（最早的日志）, 避免内存爆炸
        if (buffersToWrite.size() > 25)
        {
            std::string dropMsg =
                std::format("Dropped log messages at {}, {} larger buffers\n",
                            Timestamp::clock::now(), buffersToWrite.size() - 2);
            (void)std::fputs(dropMsg.c_str(), stderr);
            output.append({dropMsg.data(), dropMsg.size()});

            // 只保留前 2 个, 其余丢弃
            buffersToWrite.erase(buffersToWrite.begin() + 2,
                                 buffersToWrite.end());
        }

        // ── 写入 LogFile ──
        for (const auto& buffer : buffersToWrite)
        {
            output.append(buffer->view());
        }

        // 为了避免内存占用过大, 只保留前 2 个 buffer 等待回收
        if (buffersToWrite.size() > 2)
        {
            buffersToWrite.resize(2);
        }

        // ── 回收空 buffer ──
        // newBuffer1 已经被补给前端了, 这里从写完的 buffer 中回收

        if (!newBuffer1)
        {
            assert(!buffersToWrite.empty());
            newBuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer1->reset();  // 清空数据, 复用作空 buffer
        }

        if (!newBuffer2)
        {
            assert(!buffersToWrite.empty());
            newBuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer2->reset();
        }

        buffersToWrite.clear();
        output.flush();
    }

    output.flush();  // 退出前最后一次 flush
}

}  // namespace chaoxi
