#pragma once

#include <cassert>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace chaoxi
{
template <typename T>
class BlockingQueue
{
public:
    using queue_type = std::deque<T>;

    void put(const T& x)
    {
        std::scoped_lock lock(mutex_);
        queue_.push_back(x);
        notEmpty_.notify_one();  // wait morphing saves us
        // http://www.domaigne.com/blog/computing/condvars-signal-with-mutex-locked-or-not/
    }

    void put(T&& x)
    {
        std::scoped_lock lock(mutex_);
        queue_.push_back(std::move(x));
        notEmpty_.notify_one();  // wait morphing saves us
        // http://www.domaigne.com/blog/computing/condvars-signal-with-mutex-locked-or-not/
    }

    T take()
    {
        std::unique_lock lock(mutex_);
        // always use a while-loop, due to spurious wakeup
        while (queue_.empty())
        {
            // notEmpty_.wait();
            notEmpty_.wait(lock);
        }
        assert((!queue_.empty()));
        T front(std::move(queue_.front()));
        queue_.pop_front();
        return front;
    }

    queue_type drain()
    {
        queue_type queue;
        {
            std::scoped_lock lock(mutex_);
            queue = std::move(queue_);
            assert(queue_.empty());
        }
        return queue;
    }

    [[nodiscard]] size_t size() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    queue_type queue_;
};
}  // namespace chaoxi