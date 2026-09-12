#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/coro/Scheduler.hpp"
#include "chaoxi/coro/Spawn.hpp"
#include "chaoxi/coro/Task.hpp"

#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace
{

chaoxi::coro::Task<int> answer(int& starts)
{
    ++starts;
    co_return 42;
}

chaoxi::coro::Task<int> nestedAnswer(int& starts)
{
    const int value = co_await answer(starts);
    co_return value + 1;
}

chaoxi::coro::Task<int> fail()
{
    throw std::runtime_error("task failure");
    co_return 0;
}

chaoxi::coro::Task<void> collectAnswer(chaoxi::net::EventLoop& loop,
                                     chaoxi::coro::Task<int> task,
                                     int& result)
{
    result = co_await std::move(task);
    loop.quit();
}

chaoxi::coro::Task<void> collectException(chaoxi::net::EventLoop& loop,
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

chaoxi::coro::Task<void> awaitEmptyTask(chaoxi::net::EventLoop& loop,
                                      bool& rejected)
{
    chaoxi::coro::Task<int> empty;
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

chaoxi::coro::Task<void> yieldInOrder(chaoxi::net::EventLoop& loop,
                                    std::vector<int>& order)
{
    order.push_back(1);
    co_await chaoxi::coro::yield(loop);
    order.push_back(3);
    loop.quit();
}

TEST(CoroTaskTest, IsLazyAndPropagatesNestedValues)
{
    chaoxi::net::EventLoop loop;
    int starts = 0;
    int result = 0;
    auto task = nestedAnswer(starts);

    EXPECT_EQ(starts, 0);
    chaoxi::coro::spawn(loop, collectAnswer(loop, std::move(task), result));
    loop.loop();

    EXPECT_EQ(starts, 1);
    EXPECT_EQ(result, 43);
}

TEST(CoroTaskTest, PropagatesExceptionsToAwaiter)
{
    chaoxi::net::EventLoop loop;
    bool caught = false;

    chaoxi::coro::spawn(loop, collectException(loop, caught));
    loop.loop();

    EXPECT_TRUE(caught);
}

TEST(CoroTaskTest, RejectsAwaitingEmptyTask)
{
    chaoxi::net::EventLoop loop;
    bool rejected = false;

    chaoxi::coro::spawn(loop, awaitEmptyTask(loop, rejected));
    loop.loop();

    EXPECT_TRUE(rejected);
}

TEST(CoroTaskTest, YieldDefersContinuationToNextLoopTurn)
{
    chaoxi::net::EventLoop loop;
    std::vector<int> order;

    chaoxi::coro::spawn(loop, yieldInOrder(loop, order));
    loop.queueInLoop([&order] { order.push_back(2); });
    loop.wakeup();
    loop.loop();

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(CoroTaskTest, SpawnReportsDetachedTaskExceptions)
{
    chaoxi::net::EventLoop loop;
    bool handled = false;

    auto throwing = []() -> chaoxi::coro::Task<void>
    {
        throw std::runtime_error("detached failure");
        co_return;
    };

    chaoxi::coro::spawn(loop, throwing(),
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
