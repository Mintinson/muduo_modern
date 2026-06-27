///
/// @file HttpResponse.cpp
/// @brief HttpResponse 序列化实现 —— 构造完整 HTTP 响应文本
///

#include "chaoxi/net/http/HttpResponse.hpp"

#include "chaoxi/net/Buffer.hpp"

#include <array>
#include <charconv>

namespace chaoxi::net
{

/// 将 HTTP 响应序列化写入 Buffer
void HttpResponse::appendToBuffer(Buffer& output) const
{
    // 状态行: "HTTP/1.1 200 OK\r\n"
    if (version_ == HttpVersion::kHttp11)
    {
        output.append("HTTP/1.1 ");
    }
    else
    {
        output.append("HTTP/1.0 ");
    }

    // to_chars: 零分配、无 locale 开销的整数→字符串转换
    std::array<char, 32> buf{};
    auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(),
                                   static_cast<int>(statusCode_));
    output.append({buf.data(), ptr});
    output.append(" ");
    output.append(statusMessage_);
    output.append("\r\n");

    // Connection 与 Content-Length
    if (closeConnection_)
    {
        output.append("Connection: close\r\n");
    }
    else
    {
        auto [len_ptr, _] =
            std::to_chars(buf.data(), buf.data() + buf.size(), body_.size());
        output.append("Content-Length: ");
        output.append({buf.data(), len_ptr});
        output.append("\r\nConnection: Keep-Alive\r\n");
    }

    // 自定义 headers
    for (const auto& [key, value] : headers_)
    {
        output.append(key);
        output.append(": ");
        output.append(value);
        output.append("\r\n");
    }

    // body（空行 + 内容）
    output.append("\r\n");
    output.append(body_);
}

}  // namespace chaoxi::net
