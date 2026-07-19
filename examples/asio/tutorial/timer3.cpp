#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <iostream>

void print(chaoxi::net::EventLoop* loop, int* count)
{
    if (*count < 5)
    {
        LOG_INFO << *count;
        ++(*count);

        loop->runAfter(1, [capture0 = loop, capture1 = count]
                       { print(capture0, capture1); });
    }
    else
    {
        loop->quit();
    }
}

int main()
{
    chaoxi::net::EventLoop loop;
    int count = 0;
    // Note: loop.runEvery() is better for this use case.
    LOG_INFO << "Start timer3";
    loop.runAfter(1, [capture0 = &loop, capture1 = &count]
                  { print(capture0, capture1); });
    loop.loop();
    std::cout << "Final count is " << count << "\n";
}
