#include "AsyncLogging.hpp"
#include <algorithm>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

namespace chaoxi {

AsyncLogger::AsyncLogger(std::string basename, std::size_t rollSize,
                         int flushInterval)
    : basename_(std::move(basename)), rollSize_(rollSize),
      flushInterval_(flushInterval) {}

void AsyncLogger::append(std::string_view msg) {
  auto len = msg.size();

  std::lock_guard lock(mutex_);

  if (currentBuffer_->size() + len < currentBuffer_->capacity()) {
    currentBuffer_->insert(currentBuffer_->end(), msg.begin(), msg.end());

  } else {
    fullBuffers_.push_back(std::move(currentBuffer_));

    if (nextBuffer_) {
      currentBuffer_ = std::move(nextBuffer_);
    } else {
      currentBuffer_ = std::make_unique<Buffer>();
      currentBuffer_->reserve(kDefaultBufferSize);
    }

    currentBuffer_->insert(currentBuffer_->end(), msg.begin(), msg.end());
    cond_.notify_one();
  }
}

void AsyncLogger::backendThread(std::stop_token stoken) {

  LogFile output(basename_, rollSize_);

  newBuffer1_ = std::make_unique<Buffer>(kDefaultBufferSize);
  newBuffer2_ = std::make_unique<Buffer>(kDefaultBufferSize);
  std::vector<BufferPtr> bufferToWrite;

  while (!stoken.stop_requested()) {
    {
      std::unique_lock lock(mutex_);

      // 使用 condition_variable_any 的 wait_for，同时支持 stop_token
      if (fullBuffers_.empty()) {
        cond_.wait_for(lock, stoken, flushInterval_, [] { return false; });
      }
      fullBuffers_.push_back(std::move(currentBuffer_));
      currentBuffer_ = std::move(newBuffer1_);
      bufferToWrite.swap(fullBuffers_);
      if (!nextBuffer_) {
        nextBuffer_ = std::move(newBuffer2_);
      }
    }

    // write to file
    for (auto &buf : bufferToWrite) {
      output.append(buf->data(), buf->size());
    }
    output.flush();

    if (bufferToWrite.size() > 0) {
      newBuffer1_ = std::move(bufferToWrite.front());
      newBuffer1_->clear();
      if (bufferToWrite.size() > 1) {

        newBuffer2_ = std::move(bufferToWrite[1]);
        newBuffer2_->clear();
      }
    }

    fullBuffers_.clear();
  }
  output.flush();
}
} // namespace chaoxi
