#include "chaoxi/net/Timer.hpp"

namespace chaoxi::net {

void Timer::restart(Timestamp now) noexcept {
    if (repeat_) {
        // auto x = now + interval_;
        expiration_ = std::chrono::time_point_cast<Timestamp::duration>(now + interval_);
    } else {
        expiration_ = Timestamp::min();
    }
}

} // namespace chaoxi::net