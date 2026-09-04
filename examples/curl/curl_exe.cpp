#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/TimerId.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "curl.hpp"

namespace
{
using namespace std::literals;

class CurlExample final
{
public:
    explicit CurlExample(chaoxi::net::EventLoop& loop) : loop_(loop), curl_(loop)
    {
    }

    ~CurlExample()
    {
        if (timeout_)
        {
            loop_.cancel(*timeout_);
        }
    }

    CurlExample(const CurlExample&) = delete;
    CurlExample& operator=(const CurlExample&) = delete;
    CurlExample(CurlExample&&) = delete;
    CurlExample& operator=(CurlExample&&) = delete;

    void run(const std::vector<std::string_view>& urls)
    {
        transfers_.reserve(urls.size());
        for (const auto url : urls)
        {
            addTransfer(url);
        }

        remaining_ = transfers_.size();
        constexpr double timeoutSeconds = 30.0;
        timeout_ = loop_.runAfter(timeoutSeconds, [this] { timeout(); });
        loop_.loop();
    }

private:
    struct Transfer final
    {
        std::string requestedUrl;
        curl::RequestPtr request;
        std::size_t receivedBytes{};
    };

    void addTransfer(std::string_view url)
    {
        auto transfer = std::make_unique<Transfer>();
        transfer->requestedUrl = url;
        transfer->request = curl_.getUrl(transfer->requestedUrl);
        auto* const state = transfer.get();

        transfer->request->setDataCallback(
            [state](std::string_view data)
            { state->receivedBytes += data.size(); });
        transfer->request->setDoneCallback(
            [this, state](curl::Request& request, CURLcode code)
            { transferDone(*state, request, code); });
        transfers_.push_back(std::move(transfer));
    }

    void transferDone(Transfer& transfer, curl::Request& request, CURLcode code)
    {
        const auto effectiveUrl = request.effectiveUrl();
        std::println("url={} effective={} http={} bytes={} result={}",
                     transfer.requestedUrl,
                     effectiveUrl.empty() ? "(none)"sv : effectiveUrl,
                     request.responseCode(), transfer.receivedBytes,
                     curl_easy_strerror(code));

        transfer.request.reset();
        if (--remaining_ == 0)
        {
            if (timeout_)
            {
                loop_.cancel(*timeout_);
                timeout_.reset();
            }
            loop_.quit();
        }
    }

    void timeout()
    {
        timeout_.reset();
        std::println(stderr, "curl example timed out with {} request(s) pending",
                     remaining_);
        loop_.quit();
    }

    chaoxi::net::EventLoop& loop_;
    curl::Curl curl_;
    std::vector<std::unique_ptr<Transfer>> transfers_;
    std::optional<chaoxi::net::TimerId> timeout_;
    std::size_t remaining_{};
};
}  // namespace

int main(int argc, char* argv[])
{
    try
    {
        const curl::Global curlGlobal;
        chaoxi::net::EventLoop loop;

        std::vector<std::string_view> urls;
        if (argc > 1)
        {
            urls.reserve(static_cast<std::size_t>(argc - 1));
            const std::span arguments{argv, static_cast<std::size_t>(argc)};
            for (const char* argument : arguments | std::views::drop(1))
            {
                urls.emplace_back(argument);
            }
        }
        else
        {
            constexpr std::array defaults{
                "https://example.com"sv,
                "https://www.github.com"sv,
            };
            urls.assign(defaults.begin(), defaults.end());
        }

        CurlExample example(loop);
        example.run(urls);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::println(stderr, "curl example failed: {}", error.what());
        return 1;
    }
}
