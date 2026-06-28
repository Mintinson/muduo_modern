#pragma once

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/Buffer.hpp"
#include "chaoxi/net/Callbacks.hpp"
#include "chaoxi/net/Endian.hpp"
#include "chaoxi/net/TcpConnection.hpp"

#include <functional>
#include <string_view>
#include <utility>

class LengthHeaderCodec
{
public:
    using StringMessageCallback =
        std::function<void(const chaoxi::net::TcpConnectionPtr&,
                           const std::string& message,
                           chaoxi::Timestamp)>;

    explicit LengthHeaderCodec(StringMessageCallback cb)
        : messageCallback_(std::move(cb))
    {
    }

    void onMessage(const chaoxi::net::TcpConnectionPtr& conn,
                   chaoxi::net::Buffer& buf,
                   chaoxi::Timestamp receiveTime)
    {
        while (buf.readableBytes() >= kHeaderLen)  // kHeaderLen == 4
        {
            // FIXME: use Buffer::peekInt32()
            const void* data = buf.peek();
            int32_t be32 = *static_cast<const int32_t*>(data);  // SIGBUS
            const auto len =
                static_cast<int32_t>(chaoxi::net::sockets::ntoh32(be32));
            if (len > 65536 || len < 0)
            {
                LOG_ERROR << "Invalid length " << len;
                conn->shutdown();  // FIXME: disable reading
                break;
            }
            if (buf.readableBytes() >= len + kHeaderLen)
            {
                buf.retrieve(kHeaderLen);
                std::string message(buf.peek(), len);
                messageCallback_(conn, message, receiveTime);
                buf.retrieve(len);
            }
            else
            {
                break;
            }
        }
    }

    void send(chaoxi::net::TcpConnection* conn, std::string_view message)
    {
        chaoxi::net::Buffer buf;
        buf.append(message);
        auto len = static_cast<int32_t>(message.size());
        auto be32 = static_cast<int32_t>(chaoxi::net::sockets::hton32(len));
        buf.prepend(&be32, sizeof be32);
        conn->send(std::move(buf));
    }

private:
    StringMessageCallback messageCallback_;
    constexpr static size_t kHeaderLen = sizeof(int32_t);
};