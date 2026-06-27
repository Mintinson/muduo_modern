#pragma once

///
/// @file HttpRequest.hpp
/// @brief HTTP 请求对象 —— 存储 method / path / query / version / headers
///

#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/base/Utility.hpp"

#include <cassert>
#include <functional>
#include <iterator>
#include <ranges>
#include <string_view>
#include <unordered_map>

namespace chaoxi::net
{

class HttpRequest
{
public:
    enum class Method : std::uint8_t
    {
        kInvalid,
        kGet,
        kPost,
        kHead,
        kPut,
        kDelete,
    };
    enum class Version : std::uint8_t
    {
        kUnknown,
        kHttp10,
        kHttp11,
    };

    void setVersion(Version v) noexcept { version_ = v; }

    Version getVersion() const noexcept { return version_; }

    Method method() const noexcept { return method_; }

    /// 解析 method 字符串（仅支持 GET/POST/HEAD/PUT/DELETE），非法返回 false
    [[nodiscard]] bool setMethod(std::string_view method) noexcept
    {
        using namespace std::string_view_literals;
        assert(method_ == Method::kInvalid);
        if (method == "GET"sv)
        {
            method_ = Method::kGet;
        }
        else if (method == "POST"sv)
        {
            method_ = Method::kPost;
        }
        else if (method == "HEAD"sv)
        {
            method_ = Method::kHead;
        }
        else if (method == "PUT"sv)
        {
            method_ = Method::kPut;
        }
        else if (method == "DELETE"sv)
        {
            method_ = Method::kDelete;
        }
        else
        {
            method_ = Method::kInvalid;
            return false;
        }
        return true;
    }

    /// 返回 method 的字符串表示（如 "GET"），未设置返回 "UNKNOWN"
    [[nodiscard]] std::string_view methodString() const noexcept
    {
        const char* result = "UNKNOWN";
        switch (method_)
        {
            using enum Method;
            case kGet:
                result = "GET";
                break;
            case kPost:
                result = "POST";
                break;
            case kHead:
                result = "HEAD";
                break;
            case kPut:
                result = "PUT";
                break;
            case kDelete:
                result = "DELETE";
                break;
            default:
                break;
        }
        return result;
    }

    void setPath(std::string_view path) { path_.assign(path); }

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    void setQuery(std::string_view query) { query_.assign(query); }

    [[nodiscard]] const std::string& query() const noexcept { return query_; }

    void setReceiveTime(Timestamp t) { receiveTime_ = t; }

    Timestamp receiveTime() const noexcept { return receiveTime_; }

    /// 直接添加 header（key-value）
    void addHeader(std::string_view field, std::string_view value)
    {
        headers_.insert_or_assign(std::string(field), std::string(value));
    }

    /// 从原始指针区间添加 header（自动 trim value 首尾空格）
    void addHeader(const char* start, const char* colon, const char* end)
    {
        std::string_view field{start, colon};
        std::string_view value{std::next(colon), end};
        auto is_space = [](unsigned char c) { return std::isspace(c); };
        auto trimmed = value | std::views::drop_while(is_space) |
                       std::views::reverse | std::views::drop_while(is_space) |
                       std::views::reverse;
        headers_.insert_or_assign(std::string(field),
                                  std::ranges::to<std::string>(trimmed));
    }

    /// 获取 header 值，不存在返回空 string
    std::string getHeader(std::string_view field) const noexcept
    {
        std::string result;
        auto it = headers_.find(field);
        if (it != headers_.end())
        {
            result = it->second;
        }
        return result;
    }

    const auto& headers() const noexcept { return headers_; }

private:
    Method method_{Method::kInvalid};
    Version version_{Version::kUnknown};
    std::string path_;
    std::string query_;
    Timestamp receiveTime_;
    std::unordered_map<std::string,
                       std::string,
                       base::StringHash,
                       std::equal_to<>>
        headers_;
};

}  // namespace chaoxi::net
