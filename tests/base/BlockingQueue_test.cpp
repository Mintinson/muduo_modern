#include "chaoxi/base/BlockingQueue.hpp"

#include <atomic>
#include <cstddef>
#include <latch>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace
{

TEST(BlockingQueueTest, PreservesFifoOrder)
{
    chaoxi::BlockingQueue<int> queue;

    queue.put(1);
    queue.put(2);
    queue.put(3);

    EXPECT_EQ(queue.size(), 3U);
    EXPECT_EQ(queue.take(), 1);
    EXPECT_EQ(queue.take(), 2);
    EXPECT_EQ(queue.take(), 3);
    EXPECT_EQ(queue.size(), 0U);
}

TEST(BlockingQueueTest, SupportsMoveOnlyValues)
{
    chaoxi::BlockingQueue<std::unique_ptr<int>> queue;

    queue.put(std::make_unique<int>(42));
    auto value = queue.take();

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 42);
}

TEST(BlockingQueueTest, TakeWaitsForProducerAndWakesUp)
{
    chaoxi::BlockingQueue<std::string> queue;
    std::latch consumerStarted{1};
    std::string result;

    std::jthread consumer(
        [&]
        {
            consumerStarted.count_down();
            result = queue.take();
        });

    consumerStarted.wait();
    queue.put("ready");
    consumer.join();

    EXPECT_EQ(result, "ready");
}

TEST(BlockingQueueTest, DrainReturnsAllValuesAndEmptiesQueue)
{
    chaoxi::BlockingQueue<int> queue;
    for (int value = 0; value < 8; ++value)
    {
        queue.put(value);
    }

    auto drained = queue.drain();

    EXPECT_EQ(queue.size(), 0U);
    ASSERT_EQ(drained.size(), 8U);
    for (int expected = 0; expected < 8; ++expected)
    {
        EXPECT_EQ(drained.front(), expected);
        drained.pop_front();
    }
}

TEST(BlockingQueueTest, MultipleProducersAndConsumersLoseNoValues)
{
    constexpr std::size_t kProducerCount = 4;
    constexpr std::size_t kConsumerCount = 4;
    constexpr std::size_t kItemsPerProducer = 2'000;
    constexpr std::size_t kTotalItems = kProducerCount * kItemsPerProducer;

    chaoxi::BlockingQueue<std::size_t> queue;
    std::latch start{kProducerCount + kConsumerCount + 1};
    std::atomic_size_t nextTake{0};
    std::atomic_size_t invalidValues{0};
    std::vector<std::atomic_uint> occurrences(kTotalItems);
    std::vector<std::jthread> threads;
    threads.reserve(kProducerCount + kConsumerCount);

    for (std::size_t producer = 0; producer < kProducerCount; ++producer)
    {
        threads.emplace_back(
            [&, producer]
            {
                start.arrive_and_wait();
                const std::size_t first = producer * kItemsPerProducer;
                for (std::size_t offset = 0; offset < kItemsPerProducer;
                     ++offset)
                {
                    queue.put(first + offset);
                }
            });
    }

    for (std::size_t consumer = 0; consumer < kConsumerCount; ++consumer)
    {
        threads.emplace_back(
            [&]
            {
                start.arrive_and_wait();
                while (nextTake.fetch_add(1, std::memory_order_relaxed) <
                       kTotalItems)
                {
                    const std::size_t value = queue.take();
                    if (value < kTotalItems)
                    {
                        occurrences[value].fetch_add(1,
                                                     std::memory_order_relaxed);
                    }
                    else
                    {
                        invalidValues.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
    }

    start.arrive_and_wait();
    for (auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(queue.size(), 0U);
    EXPECT_EQ(invalidValues.load(std::memory_order_relaxed), 0U);
    for (const auto& count : occurrences)
    {
        EXPECT_EQ(count.load(std::memory_order_relaxed), 1U);
    }
}

}  // namespace
