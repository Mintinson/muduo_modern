#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/v2/Sleep.hpp"
#include "chaoxi/v2/Spawn.hpp"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using namespace std::chrono_literals;

chaoxi::v2::Task<void> sleepAndFinish(chaoxi::net::EventLoop& loop,
                                      bool& completed)
{
    co_await chaoxi::v2::sleepFor(loop, 2ms);
    completed = true;
    loop.quit();
}

chaoxi::v2::Task<void> recordAfter(chaoxi::net::EventLoop& loop,
                                   std::chrono::milliseconds delay,
                                   int value,
                                   std::vector<int>& order)
{
    co_await chaoxi::v2::sleepFor(loop, delay);
    order.push_back(value);
    if (order.size() == 2)
    {
        loop.quit();
    }
}

TEST(V2SleepTest, ResumesAfterDeadline)
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

    chaoxi::v2::spawn(loop, sleepAndFinish(loop, completed));
    loop.loop();

    EXPECT_TRUE(completed);
    EXPECT_FALSE(timedOut);
}

TEST(V2SleepTest, PreservesDeadlineOrder)
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

    chaoxi::v2::spawn(loop, recordAfter(loop, 10ms, 2, order));
    chaoxi::v2::spawn(loop, recordAfter(loop, 1ms, 1, order));
    loop.loop();

    EXPECT_FALSE(timedOut);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

}  // namespace
