#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"

class Printer
{
public:
    Printer(chaoxi::net::EventLoop* loop) : loop_(loop)
    {
        // Note: loop.runEvery() is better for this use case.
        loop_->runAfter(1, [this] { print(); });
    }

  ~Printer()
  {
    LOG_INFO << "Final count is " << count_;
  }

  void print()
  {
    if (count_ < 5)
    {
      LOG_INFO << count_;
      ++count_;

      loop_->runAfter(1, [this] { print(); });
    }
    else
    {
      loop_->quit();
    }
  }
private:
    chaoxi::net::EventLoop* loop_;
    int count_{};
};

int main()
{
    chaoxi::net::EventLoop loop;
    Printer printer(&loop);
    loop.loop();
}