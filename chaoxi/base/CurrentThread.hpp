#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace chaoxi::CurrentThread {
extern thread_local int t_cachedTid;
extern thread_local char t_tidString[32];
extern thread_local int t_tidStringLength;
extern thread_local const char* t_threadName;

void cacheTid();

inline int tid() noexcept {
    if (t_cachedTid == 0) [[unlikely]] {
        cacheTid();
    }
    return t_cachedTid;
}

inline std::string_view tidString() noexcept {
    return {t_tidString, static_cast<std::size_t>(t_tidStringLength)};
}

inline const char* name() noexcept {
    return t_threadName;
}

bool isMainThread() noexcept;

void sleepUsec(int64_t usec) noexcept;

std::string stackTrace(std::size_t skip = 1);

};  // namespace chaoxi::CurrentThread