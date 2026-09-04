#pragma once

#include "chaoxi/net/Platform.hpp"
#include "chaoxi/net/TimerId.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <curl/curl.h>

namespace chaoxi::net
{
class Channel;
class EventLoop;
}  // namespace chaoxi::net

namespace curl
{
/// Process-wide libcurl state. Construct before creating threads or curl
/// handles.
class Global final
{
public:
    Global();
    ~Global() noexcept;

    Global(const Global&) = delete;
    Global& operator=(const Global&) = delete;
    Global(Global&&) = delete;
    Global& operator=(Global&&) = delete;
};

class Curl;

class Request : public std::enable_shared_from_this<Request>
{
public:
    using DataCallback = std::move_only_function<void(std::string_view)>;
    using DoneCallback = std::move_only_function<void(Request&, CURLcode)>;

    Request(Curl& owner, std::string_view url);
    ~Request() noexcept;

    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
    Request(Request&&) = delete;
    Request& operator=(Request&&) = delete;

    void setDataCallback(DataCallback callback) noexcept;
    void setHeaderCallback(DataCallback callback) noexcept;
    void setDoneCallback(DoneCallback callback) noexcept;

    void headerOnly();
    void setRange(std::string_view range);

    [[nodiscard]] std::string_view effectiveUrl() const noexcept;
    [[nodiscard]] std::optional<std::string_view> redirectUrl() const noexcept;
    [[nodiscard]] long responseCode() const noexcept;

    [[nodiscard]] ::CURL* nativeHandle() const noexcept { return handle_; }

private:
    friend class Curl;

    void complete(CURLcode code);
    void receiveData(std::string_view data);
    void receiveHeader(std::string_view data);

    static std::size_t writeData(char* data,
                                 std::size_t size,
                                 std::size_t count,
                                 void* context) noexcept;
    static std::size_t writeHeader(char* data,
                                   std::size_t size,
                                   std::size_t count,
                                   void* context) noexcept;

    Curl& owner_;
    ::CURL* handle_{};
    DataCallback dataCallback_;
    DataCallback headerCallback_;
    DoneCallback doneCallback_;
};

using RequestPtr = std::shared_ptr<Request>;

/// Integrates libcurl's high-performance multi-socket API with EventLoop.
class Curl final
{
public:
    explicit Curl(chaoxi::net::EventLoop& loop);
    ~Curl() noexcept;

    Curl(const Curl&) = delete;
    Curl& operator=(const Curl&) = delete;
    Curl(Curl&&) = delete;
    Curl& operator=(Curl&&) = delete;

    [[nodiscard]] RequestPtr getUrl(std::string_view url);

    [[nodiscard]] ::CURLM* nativeHandle() const noexcept { return multiHandle_; }

private:
    friend class Request;

    using ChannelPtr = std::shared_ptr<chaoxi::net::Channel>;

    static int socketCallback(::CURL* easy,
                              ::curl_socket_t socket,
                              int action,
                              void* context,
                              void* socketContext) noexcept;
    static int timerCallback(::CURLM* multi,
                             long timeoutMilliseconds,
                             void* context) noexcept;

    int updateSocket(::curl_socket_t socket, int action, void* socketContext);
    void removeSocket(::curl_socket_t socket, void* socketContext);
    chaoxi::net::Channel& getOrCreateChannel(::curl_socket_t socket,
                                             void* socketContext);
    static void updateChannelInterest(chaoxi::net::Channel& channel, int action);
    void updateTimer(long timeoutMilliseconds);
    void performSocketAction(::curl_socket_t socket, int events) noexcept;
    void processCompletions();
    void removeChannels() noexcept;

    chaoxi::net::EventLoop& loop_;
    ::CURLM* multiHandle_{};
    std::unordered_map<chaoxi::net::SocketHandle, ChannelPtr> channels_;
    std::optional<chaoxi::net::TimerId> timer_;
    std::shared_ptr<void> lifetime_;
    std::size_t timerGeneration_{};
};
}  // namespace curl
