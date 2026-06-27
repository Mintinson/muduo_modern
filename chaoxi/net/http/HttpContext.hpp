#pragma once

///
/// @file HttpContext.hpp
/// @brief HTTP 请求解析器 —— 状态机从 Buffer 中逐行解析出 HttpRequest
///
/// 解析状态机: kExpectRequestLine → kExpectHeaders → (kExpectBody) → kGotAll
/// 支持分段到达的数据流（parseRequest 可多次调用，内部自动拼接）
///

#include "chaoxi/net/http/HttpRequest.hpp"

#include <cstdint>
#include <string_view>

namespace chaoxi::net
{

class Buffer;

class HttpContext
{
public:
    enum class HttpRequestParseState : std::uint8_t
    {
        kExpectRequestLine,  ///< 等待请求行（GET /path HTTP/1.1）
        kExpectHeaders,      ///< 等待 header 行
        kExpectBody,         ///< 等待 body（暂未实现）
        kGotAll,             ///< 解析完成
    };

    /// @return false 表示解析出错，true 表示正常（可能还没收完数据）
    bool parseRequest(Buffer& buf, Timestamp receiveTime);

    bool gotAll() const noexcept
    {
        return state_ == HttpRequestParseState::kGotAll;
    }

    /// 重置状态机，可复用解析下一个请求
    void reset() noexcept
    {
        state_ = HttpRequestParseState::kExpectRequestLine;
        HttpRequest dummy;
        std::swap(request_, dummy);
    }

    const HttpRequest& request() const noexcept { return request_; }

    HttpRequest& request() noexcept { return request_; }

private:
    /// 解析 "GET /path HTTP/1.1" 行
    bool processRequestLine(std::string_view line);

    /// 逐行解析 header，遇到空行代表 header 结束
    bool processExpectedHeader(Buffer& buf, bool& ok);

    HttpRequestParseState state_{HttpRequestParseState::kExpectRequestLine};
    HttpRequest request_;
};

}  // namespace chaoxi::net
