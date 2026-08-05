#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/v2/Scheduler.hpp"
#include "chaoxi/v2/Spawn.hpp"
#include "chaoxi/v2/Task.hpp"

#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace
{

chaoxi::v2::Task<int> answer(int& starts)
{
    ++starts;
    co_return 42;
}

chaoxi::v2::Task<int> nestedAnswer(int& starts)
{
    const int value = co_await answer(starts);
    co_return value + 1;
}

chaoxi::v2::Task<int> fail()
{
    throw std::runtime_error("task failure");
    co_return 0;
}

chaoxi::v2::Task<void> collectAnswer(chaoxi::net::EventLoop& loop,
                                     chaoxi::v2::Task<int> task,
                                     int& result)
{
    result = co_await std::move(task);
    loop.quit();
}

chaoxi::v2::Task<void> collectException(chaoxi::net::EventLoop& loop,
                                        bool& caught)
{
    try
    {
        (void)co_await fail();
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    loop.quit();
}

chaoxi::v2::Task<void> awaitEmptyTask(chaoxi::net::EventLoop& loop,
                                      bool& rejected)
{
    chaoxi::v2::Task<int> empty;
    try
    {
        (void)co_await std::move(empty);
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    loop.quit();
}

chaoxi::v2::Task<void> yieldInOrder(chaoxi::net::EventLoop& loop,
                                    std::vector<int>& order)
{
    order.push_back(1);
    co_await chaoxi::v2::yield(loop);
    order.push_back(3);
    loop.quit();
}

TEST(V2TaskTest, IsLazyAndPropagatesNestedValues)
{
    chaoxi::net::EventLoop loop;
    int starts = 0;
    int result = 0;
    auto task = nestedAnswer(starts);

    EXPECT_EQ(starts, 0);
    chaoxi::v2::spawn(loop, collectAnswer(loop, std::move(task), result));
    loop.loop();

    EXPECT_EQ(starts, 1);
    EXPECT_EQ(result, 43);
}

TEST(V2TaskTest, PropagatesExceptionsToAwaiter)
{
    chaoxi::net::EventLoop loop;
    bool caught = false;

    chaoxi::v2::spawn(loop, collectException(loop, caught));
    loop.loop();

    EXPECT_TRUE(caught);
}

TEST(V2TaskTest, RejectsAwaitingEmptyTask)
{
    chaoxi::net::EventLoop loop;
    bool rejected = false;

    chaoxi::v2::spawn(loop, awaitEmptyTask(loop, rejected));
    loop.loop();

    EXPECT_TRUE(rejected);
}

TEST(V2TaskTest, YieldDefersContinuationToNextLoopTurn)
{
    chaoxi::net::EventLoop loop;
    std::vector<int> order;

    chaoxi::v2::spawn(loop, yieldInOrder(loop, order));
    loop.queueInLoop([&order] { order.push_back(2); });
    loop.wakeup();
    loop.loop();

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(V2TaskTest, SpawnReportsDetachedTaskExceptions)
{
    chaoxi::net::EventLoop loop;
    bool handled = false;

    auto throwing = []() -> chaoxi::v2::Task<void>
    {
        throw std::runtime_error("detached failure");
        co_return;
    };

    chaoxi::v2::spawn(loop, throwing(),
                      [&](std::exception_ptr error)
                      {
                          try
                          {
                              std::rethrow_exception(error);
                          }
                          catch (const std::runtime_error&)
                          {
                              handled = true;
                          }
                          loop.quit();
                      });
    loop.loop();

    EXPECT_TRUE(handled);
}

}  // namespace
