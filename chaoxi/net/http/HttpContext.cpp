///
/// @file HttpContext.cpp
/// @brief HTTP 请求解析器实现 —— 状态机驱动的逐行解析
///

#include "chaoxi/net/http/HttpContext.hpp"

#include "chaoxi/net/Buffer.hpp"

#include <cstddef>

namespace
{
/// 去除 string_view 前导空格和 TAB
constexpr void trimLeft(std::string_view& v) noexcept
{
    v.remove_prefix(std::min(v.find_first_not_of(" \t"), v.size()));
}
}  // namespace

namespace chaoxi::net
{

/// 解析请求行: METHOD SP URL SP VERSION
bool HttpContext::processRequestLine(std::string_view line)
{
    // 提取 method
    auto space1 = line.find_first_of(' ');
    if (space1 == std::string_view::npos)
    {
        return false;
    }
    if (!request_.setMethod(line.substr(0, space1)))
    {
        return false;
    }

    line.remove_prefix(space1 + 1);

    // 提取 URL（含 path 和 query）
    size_t space2 = line.find(' ');
    if (space2 == std::string_view::npos)
    {
        return false;
    }
    std::string_view url = line.substr(0, space2);

    size_t question = url.find('?');
    if (question != std::string_view::npos)
    {
        request_.setPath(url.substr(0, question));
        request_.setQuery(url.substr(question + 1));
    }
    else
    {
        request_.setPath(url);
    }

    // 提取 version
    std::string_view version = line.substr(space2 + 1);
    if (version == "HTTP/1.1")
    {
        request_.setVersion(HttpRequest::Version::kHttp11);
    }
    else if (version == "HTTP/1.0")
    {
        request_.setVersion(HttpRequest::Version::kHttp10);
    }
    else
    {
        return false;
    }

    return true;
}

/// 主解析入口 —— 状态机循环直到无更多数据或出错
bool HttpContext::parseRequest(Buffer& buf, Timestamp receiveTime)
{
    bool ok = true;
    bool hasMore = true;
    while (hasMore)
    {
        if (state_ == HttpRequestParseState::kExpectRequestLine)
        {
            if (const char* crlf = buf.findCRLF())
            {
                std::string_view line(buf.peek(),
                                      static_cast<size_t>(crlf - buf.peek()));
                ok = processRequestLine(line);
                if (ok)
                {
                    request_.setReceiveTime(receiveTime);
                    buf.retrieveUntil(crlf + 2);
                    state_ = HttpRequestParseState::kExpectHeaders;
                }
                else
                {
                    hasMore = false;
                }
            }
            else
            {
                hasMore = false;  // 数据不足，等下次 read
            }
        }
        else if (state_ == HttpRequestParseState::kExpectHeaders)
        {
            hasMore = processExpectedHeader(buf, ok);
        }
        else if (state_ == HttpRequestParseState::kExpectBody)
        {
            hasMore = false;  // FIXME: body 解析未实现
        }
    }
    return ok;
}

/// 逐行解析 header: "Key: Value"，遇到空行表示 header 结束
bool HttpContext::processExpectedHeader(Buffer& buf, bool& ok)
{
    if (const char* crlf = buf.findCRLF())
    {
        bool hasMore = true;
        std::string_view line(buf.peek(),
                              static_cast<size_t>(crlf - buf.peek()));

        if (line.empty())
        {
            // 空行 → header 结束，进入 kGotAll
            state_ = HttpRequestParseState::kGotAll;
            hasMore = false;
        }
        else
        {
            size_t colon = line.find(':');
            if (colon != std::string_view::npos)
            {
                std::string_view key = line.substr(0, colon);
                std::string_view value = line.substr(colon + 1);
                trimLeft(value);
                request_.addHeader(key, value);
            }
            else
            {
                ok = false;
                hasMore = false;
            }
        }
        buf.retrieveUntil(crlf + 2);
        return hasMore;
    }
    return false;  // 数据不足
}

}  // namespace chaoxi::net
