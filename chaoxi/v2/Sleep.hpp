#pragma once

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/v2/Task.hpp"

#include <chrono>

namespace chaoxi::net
{
class EventLoop;
}  // namespace chaoxi::net

namespace chaoxi::v2
{

Task<void> sleepUntil(net::EventLoop& loop, Timestamp deadline);

template <typename Rep, typename Period>
Task<void> sleepFor(net::EventLoop& loop,
                    std::chrono::duration<Rep, Period> duration)
{
    const auto deadline =
        Timestamp::clock::now() +
        std::chrono::duration_cast<Timestamp::duration>(duration);
    return sleepUntil(loop, deadline);
}

}  // namespace chaoxi::v2
