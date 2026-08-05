#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThread.hpp"
#include "chaoxi/base/ProcessInfo.hpp"

#include <print>


using namespace chaoxi;
using namespace chaoxi::net;

void printInfo(EventLoop* p = nullptr) {
    std::println("print: pid={}, tid={}, loop={}", process_info::pid(),
                 CurrentThread::tid(), static_cast<void*>(p));
}

void quit(EventLoop* p) {
    printInfo(p);
    p->quit();
}

int main() {
    printInfo();

    {
        EventLoopThread thr1;  // never start
    }

    {
        // dtor calls quit()
        EventLoopThread thr2;
        EventLoop* loop = thr2.startLoop();
        loop->runInLoop(std::bind(printInfo, loop));
        CurrentThread::sleepUsec(500 * 1000);
    }

    {
        // quit() before dtor
        EventLoopThread thr3;
        EventLoop* loop = thr3.startLoop();
        loop->runInLoop(std::bind(quit, loop));
        CurrentThread::sleepUsec(500 * 1000);
    }
}
