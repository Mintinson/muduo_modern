#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThread.hpp"

#include <mutex>
#include <print>

//
// Minimize locking
//

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

    ~Printer()
    {
        // cout is not thread safe
        // std::cout << "Final count is " << count_ << "\n";
        std::println("Final count is {}", count_);
    }

    void print1()
    {
        bool shouldQuit = false;
        int count = 0;

        {
            std::scoped_lock lock(mutex_);
            if (count_ < 10)
            {
                count = count_;
                ++count_;
            }
            else
            {
                shouldQuit = true;
            }
        }

        // out of lock
        if (shouldQuit)
        {
            // printf("loop1_->quit()\n");
            loop1_->quit();
        }
        else
        {
            // cout is not thread safe
            // std::cout << "Timer 1: " << count << "\n";
            std::println("Timer 1: {}", count);
            loop1_->runAfter(1, std::bind(&Printer::print1, this));
        }
    }

    void print2()
    {
        bool shouldQuit = false;
        int count = 0;

        {
            std::scoped_lock lock(mutex_);

            if (count_ < 10)
            {
                count = count_;
                ++count_;
            }
            else
            {
                shouldQuit = true;
            }
        }

        // out of lock
        if (shouldQuit)
        {
            // printf("loop2_->quit()\n");
            loop2_->quit();
        }
        else
        {
            // cout is not thread safe
            // std::cout << "Timer 2: " << count << "\n";
            std::println("Timer 2: {}", count);

            loop2_->runAfter(1, std::bind(&Printer::print2, this));
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
    std::unique_ptr<Printer>
        printer;  // make sure printer lives longer than loops, to avoid
                  // race condition of calling print2() on destructed object.
    chaoxi::net::EventLoop loop;
    chaoxi::net::EventLoopThread loopThread;
    chaoxi::net::EventLoop* loopInAnotherThread = loopThread.startLoop();
    printer.reset(new Printer(&loop, loopInAnotherThread));
    loop.loop();
}
