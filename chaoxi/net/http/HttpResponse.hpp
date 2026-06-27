#pragma once

///
/// @file HttpResponse.hpp
/// @brief HTTP 响应对象 —— 构造状态行 / headers / body 并序列化到 Buffer
///
/// 设计要点:
///   - 使用 vector<pair> 存 headers 而非 unordered_map（响应 header
///   少，缓存友好）
///   - appendToBuffer 一次性序列化为 HTTP 文本（零格式串解析，to_chars 写数字）
///

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chaoxi::net
{

class Buffer;

class HttpResponse
{
public:
    enum class HttpStatusCode : std::uint16_t
    {
        kUnknown = 0,
        k200Ok = 200,
        k301MovedPermanently = 301,
        k400BadRequest = 400,
        k404NotFound = 404,
    };
    enum class HttpVersion : std::uint8_t
    {
        kHttp10,
        kHttp11
    };

    explicit HttpResponse(bool close) : closeConnection_(close) {}

    void setVersion(HttpVersion v) { version_ = v; }

    void setStatusCode(HttpStatusCode code) noexcept { statusCode_ = code; }

    void setStatusMessage(std::string message)
    {
        statusMessage_ = std::move(message);
    }

    void setCloseConnection(bool on) noexcept { closeConnection_ = on; }

    [[nodiscard]] bool closeConnection() const noexcept
    {
        return closeConnection_;
    }

    /// 设置 Content-Type header
    void setContentType(std::string contentType)
    {
        addHeader("Content-Type", std::move(contentType));
    }

    /// 添加自定义 header（已存在则覆盖）
    void addHeader(std::string key, std::string value)
    {
        auto it = std::ranges::find_if(headers_, [&](const auto& h)
                                       { return h.first == key; });
        if (it != headers_.end())
        {
            it->second = std::move(value);
            return;
        }
        headers_.emplace_back(std::move(key), std::move(value));
    }

    void setBody(std::string body) { body_ = std::move(body); }

    /// 将完整 HTTP 响应写入 Buffer（状态行 + headers + body）
    void appendToBuffer(Buffer& output) const;

private:
    std::vector<std::pair<std::string, std::string>> headers_;
    HttpVersion version_{HttpVersion::kHttp11};
    HttpStatusCode statusCode_{HttpStatusCode::kUnknown};
    std::string statusMessage_;
    bool closeConnection_;
    std::string body_;
};

}  // namespace chaoxi::net
