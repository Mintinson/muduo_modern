

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThread.hpp"

#include <memory>
#include <mutex>

class Printer
{
public:
    Printer(chaoxi::net::EventLoop* loop1, chaoxi::net::EventLoop* loop2)
        : loop1_(loop1)
        , loop2_(loop2)
    {
        loop1_->runAfter(1, std::bind(&Printer::print1, this));
        loop2_->runAfter(1, std::bind(&Printer::print2, this));
    }

    void print1()
    {
        std::scoped_lock lock{mutex_};

        if (count_ < 10)
        {
            LOG_INFO << "Timer1: " << count_;
            ++count_;
            loop1_->runAfter(1, std::bind(&Printer::print1, this));
        }
        else
        {
            loop1_->quit();
        }
    }

    void print2()
    {
        std::scoped_lock lock{mutex_};

        if (count_ < 10)
        {
            LOG_INFO << "Timer2: " << count_;
            ++count_;
            loop2_->runAfter(1, std::bind(&Printer::print2, this));
        }
        else
        {
            loop2_->quit();
        }
    }

private:
    std::mutex mutex_;
    chaoxi::net::EventLoop* loop1_;
    chaoxi::net::EventLoop* loop2_;
    int count_{};
};

int main()

{
    std::unique_ptr<Printer> printer;

    chaoxi::net::EventLoop loop;
    chaoxi::net::EventLoopThread loopThread;
    chaoxi::net::EventLoop* loopInAnotherThread = loopThread.startLoop();
    printer.reset(new Printer(&loop, loopInAnotherThread));
    loop.loop();
}