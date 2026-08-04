#include <atomic>
#include <chrono>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

namespace chaoxi
{

template <typename T>
class BoundedBlockingQueue
{
    // // 容量必须是 2
    // // 的幂，虽然可以通过取模解决，但位运算更快，这里为通用性保持原样
    // static_assert(Capacity >= 2, "Capacity must be at least 2");

public:
    // Keep layout stable across compiler flags because this value is part of
    // Cell's public template ABI. 64 bytes matches mainstream x86-64 cache
    // lines and the default interference size used by GCC and Clang.
    static constexpr std::size_t kCacheLineSize = 64;

    explicit BoundedBlockingQueue(std::size_t maxSize)
        : buffer_(maxSize)
        , maxSize_(maxSize)
    {
        if (maxSize == 0)
        {
            throw std::invalid_argument(
                "BoundedBlockingQueue capacity must be greater than zero");
        }
        for (size_t i = 0; i < maxSize; ++i)
        {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }

    // 禁用拷贝和赋值
    BoundedBlockingQueue(const BoundedBlockingQueue&) = delete;
    BoundedBlockingQueue& operator=(const BoundedBlockingQueue&) = delete;

    bool put(const T& data)
    {
        Cell* cell = nullptr;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        while (true)
        {
            cell = &buffer_[pos % maxSize_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);

            // 计算当前槽位序列号与我们期望的序列号(pos)之间的差异
            intptr_t diff =
                static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0)
            {
                // 如果相等，说明这个槽位刚好轮到这一轮的生产者使用
                // 尝试 CAS 抢占 `enqueue_pos_`，将其 +1
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed))
                {
                    break;  // 抢占成功，跳出循环执行写入
                }
            }
            else if (diff < 0)
            {
                // 如果 seq 落后于 pos，说明队列满了，消费者还没来得及处理这一格
                return false;
            }
            else
            {
                // 如果 diff > 0，说明有其他生产者抢先了一步，更新本地 pos 并重试
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        // 此时我们独占了这个 cell
        cell->data = data;
        // 释放给消费者：将序列号置为 pos + 1
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool take(T& record)
    {
        Cell* cell = nullptr;
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        while (true)
        {
            cell = &buffer_[pos % maxSize_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);

            // 消费者期望的序列号是 pos + 1 (因为生产者写完后设为了 pos + 1)
            intptr_t diff =
                static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0)
            {
                // 如果相等，说明这个槽位有新数据，刚好轮到这一轮的消费者使用
                if (dequeue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed))
                {
                    break;  // 抢占成功
                }
            }
            else if (diff < 0)
            {
                // 如果 seq 落后于 (pos + 1)，说明队列为空，生产者还没写到这里
                return false;
            }
            else
            {
                // 同样，被其他消费者抢先，更新本地 pos 并重试
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }

        // 此时我们独占了这个 cell
        record = cell->data;
        // 释放给下一次循环的生产者：将序列号置为 pos + Capacity
        cell->sequence.store(pos + maxSize_, std::memory_order_release);
        return true;
    }

private:
    struct alignas(kCacheLineSize) Cell
    {
        std::atomic<size_t> sequence;
        T data;
    };

    // 缓存行隔离：防止 enqueue_pos 和 dequeue_pos 发生伪共享
    alignas(kCacheLineSize) std::atomic<size_t> enqueue_pos_;
    alignas(kCacheLineSize) std::atomic<size_t> dequeue_pos_;

    // 数组也要对齐
    // alignas(kCacheLineSize) Cell buffer_[Capacity];
    std::vector<Cell> buffer_;
    const std::size_t maxSize_;
};
}  // namespace chaoxi
