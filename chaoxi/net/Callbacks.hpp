#pragma once

#include "chaoxi/base/Timestamp.hpp"

#include <functional>
#include <memory>

namespace chaoxi
{
template <typename To, typename From>
    requires std::derived_from<To, From>
[[nodiscard]] inline std::shared_ptr<To> down_pointer_cast(
    const std::shared_ptr<From>& f) noexcept
{
#ifndef NDEBUG
    // 在 Debug 模式下通过 dynamic_cast 确保实际类型真的是 To
    assert(f == nullptr || std::dynamic_pointer_cast<To>(f) != nullptr);
#endif
    // Release 模式下无额外开销
    return std::static_pointer_cast<To>(f);
}

namespace net
{
class Buffer;
class TcpConnection;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

using TimerCallback = std::function<void()>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;
using HighWaterMarkCallback =
    std::function<void(const TcpConnectionPtr&, std::size_t)>;

using MessageCallback =
    std::function<void(const TcpConnectionPtr&, Buffer&, Timestamp)>;

void defaultConnectionCallback(const TcpConnectionPtr& conn);
void defaultMessageCallback(const TcpConnectionPtr& conn,
                            Buffer& buffer,
                            Timestamp receiveTime);
}  // namespace net

}  // namespace chaoxi