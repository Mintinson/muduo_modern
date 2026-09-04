#include "curl.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/Platform.hpp"

#include <cassert>
#include <cstddef>
#include <exception>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>

namespace curl
{
namespace
{
[[noreturn]] void throwEasyError(CURLcode code, std::string_view operation)
{
    throw std::runtime_error(
        std::format("{} failed: {}", operation, curl_easy_strerror(code)));
}

void check(CURLcode code, std::string_view operation)
{
    if (code != CURLE_OK)
    {
        throwEasyError(code, operation);
    }
}

void check(CURLMcode code, std::string_view operation)
{
    if (code != CURLM_OK)
    {
        throw std::runtime_error(
            std::format("{} failed: {}", operation, curl_multi_strerror(code)));
    }
}

template <typename Value>
void setOption(::CURL* handle,
               CURLoption option,
               Value value,
               std::string_view operation)
{
    check(curl_easy_setopt(handle, option, value), operation);
}
}  // namespace

Global::Global()
{
    check(curl_global_init(CURL_GLOBAL_DEFAULT), "curl_global_init");
}

Global::~Global() noexcept
{
    curl_global_cleanup();
}

Request::Request(Curl& owner, std::string_view url)
    : owner_(owner)
    , handle_(curl_easy_init())
{
    if (handle_ == nullptr)
    {
        throw std::runtime_error("curl_easy_init failed");
    }

    try
    {
        const std::string nullTerminatedUrl{url};
        setOption(handle_, CURLOPT_URL, nullTerminatedUrl.c_str(),
                  "CURLOPT_URL");
        setOption(handle_, CURLOPT_WRITEFUNCTION, &Request::writeData,
                  "CURLOPT_WRITEFUNCTION");
        setOption(handle_, CURLOPT_WRITEDATA, this, "CURLOPT_WRITEDATA");
        setOption(handle_, CURLOPT_HEADERFUNCTION, &Request::writeHeader,
                  "CURLOPT_HEADERFUNCTION");
        setOption(handle_, CURLOPT_HEADERDATA, this, "CURLOPT_HEADERDATA");
        setOption(handle_, CURLOPT_PRIVATE, this, "CURLOPT_PRIVATE");
        setOption(handle_, CURLOPT_USERAGENT, "chaoxi-curl/1.0",
                  "CURLOPT_USERAGENT");
        setOption(handle_, CURLOPT_NOSIGNAL, 1L, "CURLOPT_NOSIGNAL");
        check(curl_multi_add_handle(owner_.nativeHandle(), handle_),
              "curl_multi_add_handle");
    }
    catch (...)
    {
        curl_easy_cleanup(std::exchange(handle_, nullptr));
        throw;
    }
}

Request::~Request() noexcept
{
    if (handle_ != nullptr)
    {
        curl_multi_remove_handle(owner_.nativeHandle(), handle_);
        curl_easy_cleanup(handle_);
    }
}

void Request::setDataCallback(DataCallback callback) noexcept
{
    dataCallback_ = std::move(callback);
}

void Request::setHeaderCallback(DataCallback callback) noexcept
{
    headerCallback_ = std::move(callback);
}

void Request::setDoneCallback(DoneCallback callback) noexcept
{
    doneCallback_ = std::move(callback);
}

void Request::headerOnly()
{
    setOption(handle_, CURLOPT_NOBODY, 1L, "CURLOPT_NOBODY");
}

void Request::setRange(std::string_view range)
{
    const std::string nullTerminatedRange{range};
    setOption(handle_, CURLOPT_RANGE, nullTerminatedRange.c_str(),
              "CURLOPT_RANGE");
}

std::string_view Request::effectiveUrl() const noexcept
{
    const char* value = nullptr;
    if (curl_easy_getinfo(handle_, CURLINFO_EFFECTIVE_URL, &value) != CURLE_OK ||
        value == nullptr)
    {
        return {};
    }
    return value;
}

std::optional<std::string_view> Request::redirectUrl() const noexcept
{
    const char* value = nullptr;
    if (curl_easy_getinfo(handle_, CURLINFO_REDIRECT_URL, &value) != CURLE_OK ||
        value == nullptr)
    {
        return std::nullopt;
    }
    return std::string_view{value};
}

long Request::responseCode() const noexcept
{
    long value{};
    if (curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &value) != CURLE_OK)
    {
        return 0;
    }
    return value;
}

void Request::complete(CURLcode code)
{
    // The callback may release the caller's RequestPtr. One atomic increment at
    // completion keeps this object and its move-only callback alive.
    [[maybe_unused]] auto keepAlive = shared_from_this();
    if (doneCallback_)
    {
        doneCallback_(*this, code);
    }
}

void Request::receiveData(std::string_view data)
{
    if (dataCallback_)
    {
        dataCallback_(data);
    }
}

void Request::receiveHeader(std::string_view data)
{
    if (headerCallback_)
    {
        headerCallback_(data);
    }
}

std::size_t Request::writeData(char* data,
                               std::size_t size,
                               std::size_t count,
                               void* context) noexcept
{
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
    {
        return 0;
    }

    const auto bytes = size * count;
    try
    {
        static_cast<Request*>(context)->receiveData({data, bytes});
        return bytes;
    }
    catch (...)
    {
        return 0;
    }
}

std::size_t Request::writeHeader(char* data,
                                 std::size_t size,
                                 std::size_t count,
                                 void* context) noexcept
{
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
    {
        return 0;
    }

    const auto bytes = size * count;
    try
    {
        static_cast<Request*>(context)->receiveHeader({data, bytes});
        return bytes;
    }
    catch (...)
    {
        return 0;
    }
}

Curl::Curl(chaoxi::net::EventLoop& loop)
    : loop_(loop)
    , multiHandle_(curl_multi_init())
    , lifetime_(std::make_shared<unsigned char>())
{
    if (multiHandle_ == nullptr)
    {
        throw std::runtime_error("curl_multi_init failed");
    }

    try
    {
        check(curl_multi_setopt(multiHandle_, CURLMOPT_SOCKETFUNCTION,
                                &Curl::socketCallback),
              "CURLMOPT_SOCKETFUNCTION");
        check(curl_multi_setopt(multiHandle_, CURLMOPT_SOCKETDATA, this),
              "CURLMOPT_SOCKETDATA");
        check(curl_multi_setopt(multiHandle_, CURLMOPT_TIMERFUNCTION,
                                &Curl::timerCallback),
              "CURLMOPT_TIMERFUNCTION");
        check(curl_multi_setopt(multiHandle_, CURLMOPT_TIMERDATA, this),
              "CURLMOPT_TIMERDATA");
    }
    catch (...)
    {
        curl_multi_cleanup(std::exchange(multiHandle_, nullptr));
        throw;
    }
}

Curl::~Curl() noexcept
{
    lifetime_.reset();
    ++timerGeneration_;
    if (timer_)
    {
        loop_.cancel(*timer_);
        timer_.reset();
    }

    if (multiHandle_ != nullptr)
    {
        curl_multi_setopt(multiHandle_, CURLMOPT_SOCKETFUNCTION,
                          static_cast<curl_socket_callback>(nullptr));
        curl_multi_setopt(multiHandle_, CURLMOPT_TIMERFUNCTION,
                          static_cast<curl_multi_timer_callback>(nullptr));
        curl_multi_cleanup(multiHandle_);
    }
    removeChannels();
}

RequestPtr Curl::getUrl(std::string_view url)
{
    return std::make_shared<Request>(*this, url);
}

int Curl::socketCallback(::CURL* /*easy*/,
                         ::curl_socket_t socket,
                         int action,
                         void* context,
                         void* socketContext) noexcept
{
    try
    {
        return static_cast<Curl*>(context)->updateSocket(socket, action,
                                                         socketContext);
    }
    catch (const std::exception& error)
    {
        LOG_ERROR << "libcurl socket callback failed: " << error.what();
        return -1;
    }
    catch (...)
    {
        LOG_ERROR << "libcurl socket callback failed with an unknown error";
        return -1;
    }
}

int Curl::timerCallback(::CURLM* /*multi*/,
                        long timeoutMilliseconds,
                        void* context) noexcept
{
    try
    {
        static_cast<Curl*>(context)->updateTimer(timeoutMilliseconds);
        return 0;
    }
    catch (const std::exception& error)
    {
        LOG_ERROR << "libcurl timer callback failed: " << error.what();
        return -1;
    }
    catch (...)
    {
        LOG_ERROR << "libcurl timer callback failed with an unknown error";
        return -1;
    }
}

int Curl::updateSocket(::curl_socket_t socket, int action, void* socketContext)
{
    if (action == CURL_POLL_REMOVE)
    {
        removeSocket(socket, socketContext);
        return 0;
    }

    auto& channel = getOrCreateChannel(socket, socketContext);
    updateChannelInterest(channel, action);
    return 0;
}

void Curl::removeSocket(::curl_socket_t socket, void* socketContext)
{
    auto* rawChannel = static_cast<chaoxi::net::Channel*>(socketContext);
    if (rawChannel == nullptr)
    {
        return;
    }

    const auto fd = static_cast<chaoxi::net::SocketHandle>(socket);
    const auto found = channels_.find(fd);
    assert(found != channels_.end() && found->second.get() == rawChannel);
    auto channel = found->second;
    if (!channel->isNoneEvent())
    {
        channel->disableAll();
    }
    channel->setReadCallback([](auto) {});
    channel->setWriteCallback([] {});

    std::weak_ptr<void> lifetime = lifetime_;
    loop_.queueInLoop(
        [this, fd, channel = std::move(channel), lifetime = std::move(lifetime)]
        {
            const auto keepAlive = lifetime.lock();
            if (!keepAlive)
            {
                return;
            }

            const auto current = channels_.find(fd);
            if (current != channels_.end() && current->second == channel &&
                channel->isNoneEvent())
            {
                channel->remove();
                channels_.erase(current);
            }
        });
    check(curl_multi_assign(multiHandle_, socket, nullptr), "curl_multi_assign");
}

chaoxi::net::Channel& Curl::getOrCreateChannel(::curl_socket_t socket,
                                               void* socketContext)
{
    const auto fd = static_cast<chaoxi::net::SocketHandle>(socket);
    auto* rawChannel = static_cast<chaoxi::net::Channel*>(socketContext);
    if (rawChannel != nullptr)
    {
        [[maybe_unused]] const auto found = channels_.find(fd);
        assert(found != channels_.end() && found->second.get() == rawChannel);
        return *rawChannel;
    }

    const auto found = channels_.find(fd);
    auto channel = found != channels_.end()
                       ? found->second
                       : std::make_shared<chaoxi::net::Channel>(&loop_, fd);
    rawChannel = channel.get();
    channel->tie(channel);
    channel->setReadCallback([this, socket](auto)
                             { performSocketAction(socket, CURL_CSELECT_IN); });
    channel->setWriteCallback(
        [this, socket] { performSocketAction(socket, CURL_CSELECT_OUT); });
    if (found == channels_.end())
    {
        channels_.emplace(fd, std::move(channel));
    }
    check(curl_multi_assign(multiHandle_, socket, rawChannel),
          "curl_multi_assign");
    return *rawChannel;
}

void Curl::updateChannelInterest(chaoxi::net::Channel& channel, int action)
{
    const auto unsignedAction = static_cast<unsigned int>(action);
    const bool wantsRead =
        (unsignedAction & static_cast<unsigned int>(CURL_POLL_IN)) != 0U;
    const bool wantsWrite =
        (unsignedAction & static_cast<unsigned int>(CURL_POLL_OUT)) != 0U;
    if (channel.isReading() != wantsRead)
    {
        if (wantsRead)
        {
            channel.enableReading();
        }
        else
        {
            channel.disableReading();
        }
    }
    if (channel.isWriting() != wantsWrite)
    {
        if (wantsWrite)
        {
            channel.enableWriting();
        }
        else
        {
            channel.disableWriting();
        }
    }
}

void Curl::updateTimer(long timeoutMilliseconds)
{
    ++timerGeneration_;
    if (timer_)
    {
        loop_.cancel(*timer_);
        timer_.reset();
    }
    if (timeoutMilliseconds < 0)
    {
        return;
    }

    const auto generation = timerGeneration_;
    std::weak_ptr<void> lifetime = lifetime_;
    constexpr double millisecondsPerSecond = 1000.0;
    timer_ = loop_.runAfter(static_cast<double>(timeoutMilliseconds) /
                                millisecondsPerSecond,
                            [this, generation, lifetime = std::move(lifetime)]
                            {
                                const auto keepAlive = lifetime.lock();
                                if (!keepAlive || generation != timerGeneration_)
                                {
                                    return;
                                }
                                timer_.reset();
                                performSocketAction(CURL_SOCKET_TIMEOUT, 0);
                            });
}

void Curl::performSocketAction(::curl_socket_t socket, int events) noexcept
{
    int runningHandles{};
    const auto result =
        curl_multi_socket_action(multiHandle_, socket, events, &runningHandles);
    if (result != CURLM_OK)
    {
        LOG_ERROR << "curl_multi_socket_action failed: "
                  << curl_multi_strerror(result);
        loop_.quit();
        return;
    }
    processCompletions();
}

void Curl::processCompletions()
{
    int remainingMessages{};
    while (auto* message =
               curl_multi_info_read(multiHandle_, &remainingMessages))
    {
        if (message->msg != CURLMSG_DONE)
        {
            continue;
        }

        Request* request = nullptr;
        const auto result =
            curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &request);
        if (result != CURLE_OK || request == nullptr)
        {
            LOG_ERROR << "Completed curl request has no Request context";
            continue;
        }

        try
        {
            request->complete(message->data.result);
        }
        catch (const std::exception& error)
        {
            LOG_ERROR << "Curl completion callback failed: " << error.what();
            loop_.quit();
        }
        catch (...)
        {
            LOG_ERROR << "Curl completion callback failed with an unknown error";
            loop_.quit();
        }
    }
}

void Curl::removeChannels() noexcept
{
    for (const auto& [fd, channel] : channels_)
    {
        (void)fd;
        channel->setReadCallback([](auto) {});
        channel->setWriteCallback([] {});
        if (!channel->isNoneEvent())
        {
            channel->disableAll();
        }
        if (loop_.hasChannel(channel.get()))
        {
            channel->remove();
        }
    }
    channels_.clear();
}
}  // namespace curl
