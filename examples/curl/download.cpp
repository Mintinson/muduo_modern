// Concurrently download one file over HTTP.

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <curl/curl.h>

#include "curl.hpp"

namespace
{
using chaoxi::net::EventLoop;
using curl::Request;
using curl::RequestPtr;
using namespace std::literals;

[[nodiscard]] bool asciiCaseInsensitiveEqual(std::string_view lhs,
                                             std::string_view rhs)
{
    return lhs.size() == rhs.size() &&
           std::ranges::equal(
               lhs, rhs, [](unsigned char left, unsigned char right)
               { return std::tolower(left) == std::tolower(right); });
}

[[nodiscard]] std::string_view trim(std::string_view text)
{
    constexpr auto whitespace = " \t\r\n"sv;
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

class Piece final
{
public:
    Piece(RequestPtr request,
          std::ofstream output,
          std::string range,
          std::move_only_function<void(CURLcode)> onDone)
        : request_(std::move(request))
        , output_(std::move(output))
        , range_(std::move(range))
        , onDone_(std::move(onDone))
    {
        LOG_INFO << "range: " << range_;
        request_->setRange(range_);
        request_->setDataCallback([this](std::string_view data)
                                  { write(data); });
        request_->setDoneCallback([this](Request&, CURLcode code)
                                  { finish(code); });
    }

private:
    void write(std::string_view data)
    {
        output_.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    void finish(CURLcode code)
    {
        LOG_INFO << '[' << range_
                 << "] is done, curl code=" << static_cast<int>(code);
        request_.reset();
        output_.close();
        onDone_(code);
    }

    RequestPtr request_;
    std::ofstream output_;
    std::string range_;
    std::move_only_function<void(CURLcode)> onDone_;
};

class Downloader final
{
public:
    Downloader(EventLoop& loop, std::string url)
        : loop_(loop)
        , curl_(loop_)
        , url_(std::move(url))
        , headerRequest_(curl_.getUrl(url_))
    {
        headerRequest_->setHeaderCallback([this](std::string_view data)
                                          { processHeader(data); });
        headerRequest_->setDoneCallback([this](Request& request, CURLcode code)
                                        { headerDone(request, code); });
        headerRequest_->headerOnly();
    }

private:
    static constexpr std::size_t kConcurrent = 4;
    static constexpr std::int64_t kMinimumPieceSize = 4096;
    static constexpr long kHttpOk = 200;

    void processHeader(std::string_view header)
    {
        const auto separator = header.find(':');
        if (separator == std::string_view::npos)
        {
            return;
        }

        const auto name = trim(header.substr(0, separator));
        const auto value = trim(header.substr(separator + 1));
        if (asciiCaseInsensitiveEqual(name, "Accept-Ranges"sv))
        {
            acceptRanges_ = asciiCaseInsensitiveEqual(value, "bytes"sv);
        }
        else if (asciiCaseInsensitiveEqual(name, "Content-Length"sv))
        {
            std::int64_t parsedLength{};
            const auto [end, error] =
                std::from_chars(value.begin(), value.end(), parsedLength);
            if (error == std::errc{} && end == value.end() && parsedLength >= 0)
            {
                length_ = parsedLength;
                LOG_INFO << "Content-Length: " << length_;
            }
        }
    }

    void headerDone(Request& request, CURLcode curlCode)
    {
        const long responseCode = request.responseCode();
        if (curlCode != CURLE_OK || responseCode != kHttpOk)
        {
            LOG_ERROR << "Header request failed: HTTP " << responseCode
                      << ", curl code=" << static_cast<int>(curlCode);
            loop_.quit();
            return;
        }

        headerRequest_.reset();
        if (acceptRanges_ && length_ >= static_cast<std::int64_t>(kConcurrent) *
                                            kMinimumPieceSize)
        {
            LOG_INFO << "Downloading with " << kConcurrent << " connections";
            remaining_ = static_cast<int>(kConcurrent);
            startConcurrentDownload();
        }
        else
        {
            LOG_WARN << "Single connection download";
            startSingleDownload();
        }
    }

    void startSingleDownload()
    {
        output_.open("output", std::ios::binary | std::ios::trunc);
        if (!output_)
        {
            LOG_ERROR << "Cannot create output file";
            loop_.quit();
            return;
        }

        downloadRequest_ = curl_.getUrl(url_);
        downloadRequest_->setDataCallback([this](std::string_view data)
                                          { write(data); });
        downloadRequest_->setDoneCallback([this](Request&, CURLcode code)
                                          { downloadDone(code); });
        remaining_ = 1;
    }

    void startConcurrentDownload()
    {
        const std::int64_t pieceLength =
            length_ / static_cast<std::int64_t>(kConcurrent);
        auto piece = pieces_.begin();
        for (std::size_t index = 0; index < kConcurrent; ++index, ++piece)
        {
            const auto filename =
                std::format("output-{:05}-of-{:05}", index, kConcurrent);
            std::ofstream output(filename, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                LOG_ERROR << "Cannot create output file: " << filename;
                loop_.quit();
                return;
            }

            const auto first = static_cast<std::int64_t>(index) * pieceLength;
            const auto last =
                index + 1 < kConcurrent ? first + pieceLength - 1 : length_ - 1;
            *piece = std::make_unique<Piece>(
                curl_.getUrl(url_), std::move(output),
                std::format("{}-{}", first, last),
                [this](CURLcode code) { downloadDone(code); });
        }
    }

    void write(std::string_view data)
    {
        output_.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    void downloadDone(CURLcode curlCode)
    {
        if (curlCode != CURLE_OK)
        {
            LOG_ERROR << "Download failed: curl code="
                      << static_cast<int>(curlCode);
        }

        if (--remaining_ == 0)
        {
            output_.close();
            loop_.quit();
        }
    }

    EventLoop& loop_;
    curl::Curl curl_;
    std::string url_;
    RequestPtr headerRequest_;
    RequestPtr downloadRequest_;
    std::ofstream output_;
    std::array<std::unique_ptr<Piece>, kConcurrent> pieces_{};
    std::int64_t length_{};
    int remaining_{};
    bool acceptRanges_{};
};
}  // namespace

int main(int argc, char* argv[])
{
    const curl::Global curlGlobal;
    EventLoop loop;
    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    const std::string url =
        arguments.size() > 1
            ? arguments[1]
            : "https://chenshuo-public.s3.amazonaws.com/pdf/allinone.pdf";

    Downloader downloader(loop, url);
    loop.loop();
}
