#include "chaoxi/net/Timer.hpp"

#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

namespace
{

using namespace std::chrono_literals;

TEST(TimerTest, RunsCallback)
{
    int calls = 0;
    chaoxi::net::Timer timer([&] { ++calls; }, chaoxi::Timestamp{}, 0.0);

    timer.run();
    timer.run();

    EXPECT_EQ(calls, 2);
    EXPECT_FALSE(timer.repeat());
}

TEST(TimerTest, OneShotRestartExpiresPermanently)
{
    chaoxi::net::Timer timer([] {}, chaoxi::Timestamp{} + 1s, 0.0);

    timer.restart(chaoxi::Timestamp{} + 5s);

    EXPECT_EQ(timer.expiration(), chaoxi::Timestamp::min());
}

TEST(TimerTest, RepeatingRestartUsesCurrentTime)
{
    const chaoxi::Timestamp now = chaoxi::Timestamp{} + 10s;
    chaoxi::net::Timer timer([] {}, chaoxi::Timestamp{} + 1s, 0.25);

    timer.restart(now);

    EXPECT_TRUE(timer.repeat());
    EXPECT_EQ(timer.expiration(), now + 250ms);
}

TEST(TimerTest, SequenceNumbersAreUniqueAndMonotonic)
{
    const std::int64_t before = chaoxi::net::Timer::numCreated();
    chaoxi::net::Timer first([] {}, chaoxi::Timestamp{}, 0.0);
    chaoxi::net::Timer second([] {}, chaoxi::Timestamp{}, 0.0);

    EXPECT_EQ(first.sequence(), before + 1);
    EXPECT_EQ(second.sequence(), before + 2);
    EXPECT_EQ(chaoxi::net::Timer::numCreated(), before + 2);
}

}  // namespace
