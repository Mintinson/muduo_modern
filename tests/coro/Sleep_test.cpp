#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/coro/Sleep.hpp"
#include "chaoxi/coro/Spawn.hpp"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using namespace std::chrono_literals;

chaoxi::coro::Task<void> sleepAndFinish(chaoxi::net::EventLoop& loop,
                                      bool& completed)
{
    co_await chaoxi::coro::sleepFor(loop, 2ms);
    completed = true;
    loop.quit();
}

chaoxi::coro::Task<void> recordAfter(chaoxi::net::EventLoop& loop,
                                   std::chrono::milliseconds delay,
                                   int value,
                                   std::vector<int>& order)
{
    co_await chaoxi::coro::sleepFor(loop, delay);
    order.push_back(value);
    if (order.size() == 2)
    {
        loop.quit();
    }
}

TEST(CoroSleepTest, ResumesAfterDeadline)
{
    chaoxi::net::EventLoop loop;
    bool completed = false;
    bool timedOut = false;
    loop.runAfter(1.0,
                  [&]
                  {
                      timedOut = true;
                      loop.quit();
                  });

    chaoxi::coro::spawn(loop, sleepAndFinish(loop, completed));
    loop.loop();

    EXPECT_TRUE(completed);
    EXPECT_FALSE(timedOut);
}

TEST(CoroSleepTest, PreservesDeadlineOrder)
{
    chaoxi::net::EventLoop loop;
    std::vector<int> order;
    bool timedOut = false;
    loop.runAfter(1.0,
                  [&]
                  {
                      timedOut = true;
                      loop.quit();
                  });

    chaoxi::coro::spawn(loop, recordAfter(loop, 10ms, 2, order));
    chaoxi::coro::spawn(loop, recordAfter(loop, 1ms, 1, order));
    loop.loop();

    EXPECT_FALSE(timedOut);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

}  // namespace
