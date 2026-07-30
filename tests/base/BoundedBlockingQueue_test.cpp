#include "chaoxi/base/BoundedBlockingQueue.hpp"

#include <atomic>
#include <cstddef>
#include <latch>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace
{

TEST(BoundedBlockingQueueTest, ReportsEmptyAndFullWithoutBlocking)
{
    chaoxi::BoundedBlockingQueue<int> queue(2);
    int value = -1;

    EXPECT_FALSE(queue.take(value));
    EXPECT_TRUE(queue.put(10));
    EXPECT_TRUE(queue.put(20));
    EXPECT_FALSE(queue.put(30));

    EXPECT_TRUE(queue.take(value));
    EXPECT_EQ(value, 10);
    EXPECT_TRUE(queue.take(value));
    EXPECT_EQ(value, 20);
    EXPECT_FALSE(queue.take(value));
}

TEST(BoundedBlockingQueueTest, RejectsZeroCapacity)
{
    EXPECT_THROW((chaoxi::BoundedBlockingQueue<int>(0)), std::invalid_argument);
}

TEST(BoundedBlockingQueueTest, ReusesSlotsAndPreservesFifoOrder)
{
    chaoxi::BoundedBlockingQueue<int> queue(3);
    int value = 0;

    for (int round = 0; round < 100; ++round)
    {
        for (int offset = 0; offset < 3; ++offset)
        {
            ASSERT_TRUE(queue.put(round * 3 + offset));
        }
        EXPECT_FALSE(queue.put(-1));

        for (int offset = 0; offset < 3; ++offset)
        {
            ASSERT_TRUE(queue.take(value));
            EXPECT_EQ(value, round * 3 + offset);
        }
        EXPECT_FALSE(queue.take(value));
    }
}

TEST(BoundedBlockingQueueTest, PublishesCompleteObjects)
{
    chaoxi::BoundedBlockingQueue<std::string> queue(1);
    std::string value;

    ASSERT_TRUE(queue.put(std::string(4'096, 'x')));
    ASSERT_TRUE(queue.take(value));

    EXPECT_EQ(value, std::string(4'096, 'x'));
}

TEST(BoundedBlockingQueueTest, IsNeitherCopyableNorAssignable)
{
    using Queue = chaoxi::BoundedBlockingQueue<int>;

    static_assert(!std::is_copy_constructible_v<Queue>);
    static_assert(!std::is_copy_assignable_v<Queue>);
    SUCCEED();
}

TEST(BoundedBlockingQueueTest, MultipleProducersAndConsumersLoseNoValues)
{
    constexpr std::size_t kProducerCount = 4;
    constexpr std::size_t kConsumerCount = 4;
    constexpr std::size_t kItemsPerProducer = 5'000;
    constexpr std::size_t kTotalItems = kProducerCount * kItemsPerProducer;

    chaoxi::BoundedBlockingQueue<std::size_t> queue(256);
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
                    const std::size_t value = first + offset;
                    while (!queue.put(value))
                    {
                        std::this_thread::yield();
                    }
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
                    std::size_t value = 0;
                    while (!queue.take(value))
                    {
                        std::this_thread::yield();
                    }
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

    std::size_t ignored = 0;
    EXPECT_FALSE(queue.take(ignored));
    EXPECT_EQ(invalidValues.load(std::memory_order_relaxed), 0U);
    for (const auto& count : occurrences)
    {
        EXPECT_EQ(count.load(std::memory_order_relaxed), 1U);
    }
}

}  // namespace
