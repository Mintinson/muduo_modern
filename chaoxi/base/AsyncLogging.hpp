#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace chaoxi {

class AsyncLogger {
public:
  static constexpr std::size_t kDefaultBufferSize = 4 * 1024 * 1024; // 4MB
  using Buffer = std::vector<char>;
  using BufferPtr = std::unique_ptr<Buffer>;

  AsyncLogger(std::string basename, std::size_t rollSize,
              int flushInterval = 3);
  ~AsyncLogger();

  void append(std::string_view msg);

private:
  void backendThread(std::stop_token stoken);

  const std::string basename_;
  const std::size_t rollSize_;
  const std::chrono::seconds flushInterval_;

  std::mutex mutex_;
  std::condition_variable_any cond_;

  BufferPtr currentBuffer_;
  BufferPtr nextBuffer_;

  std::vector<BufferPtr> fullBuffers_;

  BufferPtr newBuffer1_;
  BufferPtr newBuffer2_;

  std::jthread backendThread_;
};
} // namespace chaoxi
