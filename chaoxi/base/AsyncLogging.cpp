

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
    , nextBuffer_(std::make_unique<Buffer>())     // 前端备用缓冲
{
    currentBuffer_->bzero();
    nextBuffer_->bzero();
    buffers_.reserve(
        16);  // 预分配队列空间 NOLINT(cppcoreguidelines-avoid-magic-numbers,
              // readability-magic-numbers)
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
    std::scoped_lock lock(mutex_);

    if (!running_.load(std::memory_order_relaxed) ||
        msg.size() >= kDefaultBufferSize)
    {
        droppedMessages_.fetch_add(1, std::memory_order_relaxed);
        droppedBytes_.fetch_add(msg.size(), std::memory_order_relaxed);
        return;
    }

    if (currentBuffer_->avail() > msg.size())  // 最常见情况：空间够
    {
        // ── 快速路径：当前缓冲有足够空间，直接写入 ──
        currentBuffer_->append(msg);
        ++currentBufferMessages_;
    }
    else
    {
        // ── 慢速路径：当前缓冲已满 ──
        // ① 把已满的当前缓冲移动到待写队列。
        buffers_.push_back({.buffer=std::move(currentBuffer_), .messages=currentBufferMessages_});

        // ② 从前端备用缓冲中取出一个新缓冲（如果仍有备用）。
        if (nextBuffer_) [[likely]]
        {
            currentBuffer_ = std::move(nextBuffer_);
        }
        else
        {
            // 极少发生：前端写得太快，后端尚未来得及归还备用缓冲。
            currentBuffer_ = std::make_unique<Buffer>();
        }

        // ③ 写入新缓冲。
        currentBuffer_->append(msg);
        currentBufferMessages_ = 1;
        // ④ 唤醒后端线程来消费
        cond_.notify_one();
    }

    ++acceptedMessages_;
    acceptedBytes_ += msg.size();
}

void AsyncLogging::start()
{
    running_.store(true, std::memory_order_release);
    thread_ = std::jthread([this] { threadFunc(); });
    // 启动闩锁确保 start() 返回时，后端线程已进入主函数并完成初始化。
    latch_.wait();
}

void AsyncLogging::stop()
{
    if (!running_.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }
    cond_.notify_one();  // 唤醒正在等待的后端线程，使其完成排空并退出。
    if (thread_.joinable())
    {
        thread_.join();
    }
}

AsyncLogging::Statistics AsyncLogging::statistics() const noexcept
{
    std::scoped_lock lock(mutex_);
    return {
        .acceptedMessages = acceptedMessages_,
        .acceptedBytes = acceptedBytes_,
        .writtenMessages = writtenMessages_.load(std::memory_order_relaxed),
        .writtenBytes = writtenBytes_.load(std::memory_order_relaxed),
        .droppedMessages = droppedMessages_.load(std::memory_order_relaxed),
        .droppedBytes = droppedBytes_.load(std::memory_order_relaxed),
        .droppedBuffers = droppedBuffers_.load(std::memory_order_relaxed),
    };
}

void AsyncLogging::
    threadFunc()  // NOLINT(readability-function-cognitive-complexity)
{
    latch_.count_down();  // 通知启动方：后端线程已经就绪。

    // 打开日志文件（不需要线程安全, 只在后端线程访问）
    LogFile output(basename_, rollSize_, false);

    // 后端空缓冲池：用于补给前端。
    auto newBuffer1 = std::make_unique<Buffer>();  // 后端预备的两个空闲缓冲
    auto newBuffer2 = std::make_unique<Buffer>();
    newBuffer1->bzero();
    newBuffer2->bzero();

    BufferVector buffersToWrite;  // 后端本次要写入文件的缓冲集合
    buffersToWrite.reserve(kBufferToWriteDefaultCap);

    while (true)
    {
        assert(newBuffer1 && newBuffer1->length() == 0);
        assert(newBuffer2 && newBuffer2->length() == 0);
        assert(buffersToWrite.empty());

        {
            std::unique_lock<std::mutex> lock(mutex_);

            // 等待数据到来，或在刷新间隔到期后被唤醒。
            cond_.wait_for(lock, std::chrono::seconds(flushInterval_),
                           [this]
                           {
                               return !buffers_.empty() ||
                                      !running_.load(std::memory_order_acquire);
                           });

            // ── 三步交换 ──
            // ① 把非空的当前缓冲加入队列；定期唤醒负责刷新短尾数据，
            //    停止唤醒负责最终排空。
            if (currentBufferMessages_ != 0)
            {
                buffers_.push_back(
                    {std::move(currentBuffer_), currentBufferMessages_});
                currentBufferMessages_ = 0;
            }

            if (buffers_.empty() && !running_.load(std::memory_order_acquire))
            {
                break;
            }

            // ② 用后端空缓冲补给前端，作为新的当前缓冲。
            if (!currentBuffer_)
            {
                currentBuffer_ = std::move(newBuffer1);
            }

            // ③ 交换队列：后端取得所有待写缓冲，前端队列变为空。
            buffersToWrite.swap(buffers_);

            // 补给前端的备用缓冲。
            if (!nextBuffer_)
            {
                nextBuffer_ = std::move(newBuffer2);
            }
        }

        assert(!buffersToWrite.empty());

        // ── 积压保护 ──
        // 如果待写缓冲超过 25 个，说明后端跟不上前端速度。
        // 丢弃多余的, 只保留前 2 个（最早的日志）, 避免内存爆炸
        if (buffersToWrite.size() > kBufferToWriteMaxSize)
        {
            std::uint64_t droppedMessages = 0;
            std::uint64_t droppedBytes = 0;
            for (auto iterator = buffersToWrite.begin() + 2;
                 iterator != buffersToWrite.end(); ++iterator)
            {
                droppedMessages += iterator->messages;
                droppedBytes += iterator->buffer->length();
            }
            droppedMessages_.fetch_add(droppedMessages,
                                       std::memory_order_relaxed);
            droppedBytes_.fetch_add(droppedBytes, std::memory_order_relaxed);
            droppedBuffers_.fetch_add(buffersToWrite.size() - 2,
                                      std::memory_order_relaxed);
            std::string dropMsg =
                std::format("Dropped log messages at {}, {} larger buffers\n",
                            Timestamp::clock::now(), buffersToWrite.size() - 2);
            (void)std::fputs(dropMsg.c_str(), stderr);
            output.append({dropMsg.data(), dropMsg.size()});

            // 只保留前 2 个, 其余丢弃
            buffersToWrite.erase(buffersToWrite.begin() + 2,
                                 buffersToWrite.end());
        }

        // ── 写入日志文件 ──
        for (const auto& buffer : buffersToWrite)
        {
            output.append(buffer.buffer->view());
            writtenMessages_.fetch_add(buffer.messages,
                                       std::memory_order_relaxed);
            writtenBytes_.fetch_add(buffer.buffer->length(),
                                    std::memory_order_relaxed);
        }

        // 为避免内存占用过大，只保留前两个缓冲等待回收。
        if (buffersToWrite.size() > 2)
        {
            buffersToWrite.resize(2);
        }

        // ── 回收空缓冲 ──
        // 后端空缓冲已经补给前端，此处从写完的缓冲中回收。

        if (!newBuffer1)
        {
            assert(!buffersToWrite.empty());
            newBuffer1 = std::move(buffersToWrite.back().buffer);
            buffersToWrite.pop_back();
            (*newBuffer1).reset();  // 清空数据并复用为空缓冲。
        }

        if (!newBuffer2)
        {
            assert(!buffersToWrite.empty());
            newBuffer2 = std::move(buffersToWrite.back().buffer);
            buffersToWrite.pop_back();
            (*newBuffer2).reset();
        }

        buffersToWrite.clear();
        output.flush();
    }

    output.flush();  // 退出前做最后一次刷新。
}

}  // namespace chaoxi
