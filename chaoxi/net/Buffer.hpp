#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chaoxi::net {
class Buffer {
public:
    static constexpr std::size_t kCheapPrepend = 8;
    static constexpr std::size_t kInitialSize = 1024;
    static constexpr std::string_view kCRLF = "\r\n";

    explicit Buffer(std::size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize) {
        assert(readableBytes() == 0);
        assert(writableBytes() == initialSize);
        assert(prependableBytes() == kCheapPrepend);
    }

    void swap(Buffer& rhs) noexcept {
        buffer_.swap(rhs.buffer_);
        std::swap(readerIndex_, rhs.readerIndex_);
        std::swap(writerIndex_, rhs.writerIndex_);
    }

    [[nodiscard]] std::size_t readableBytes() const noexcept {
        return writerIndex_ - readerIndex_;
    }

    [[nodiscard]] std::size_t writableBytes() const noexcept {
        return buffer_.size() - writerIndex_;
    }

    [[nodiscard]] std::size_t prependableBytes() const noexcept {
        return readerIndex_;
    }

    [[nodiscard]] const char* peek() const noexcept {
        return begin() + readerIndex_;
    }

    [[nodiscard]] std::span<const char> readableSpan() const noexcept {
        return {peek(), readableBytes()};
    }

    [[nodiscard]] std::span<char> writableSpan() noexcept {
        return {beginWrite(), writableBytes()};
    }

    [[nodiscard]] const char* findCRLF() const noexcept {
        auto span = readableSpan();
        auto it = std::ranges::search(span, kCRLF).begin();
        return it == span.end() ? nullptr : &*it;
    }

    [[nodiscard]] const char* findCRLF(const char* start) const noexcept {
        assert(peek() <= start);
        assert(start <= beginWrite());
        auto span = std::span<const char>{
            start, static_cast<std::size_t>(beginWrite() - start)};
        auto it = std::ranges::search(span, kCRLF).begin();
        return it == span.end() ? nullptr : &*it;
    }

    [[nodiscard]] const char* findEOL() const noexcept {
        const void* eol = std::memchr(peek(), '\n', readableBytes());
        return static_cast<const char*>(eol);
    }

    [[nodiscard]] const char* findEOL(const char* start) const noexcept {
        assert(peek() <= start);
        assert(start <= beginWrite());
        const void* eol = std::memchr(start, '\n', beginWrite() - start);
        return static_cast<const char*>(eol);
    }

    void retrieve(std::size_t len) noexcept {
        assert(len <= readableBytes());
        if (len < readableBytes()) {
            readerIndex_ += len;
        } else {
            retrieveAll();
        }
    }

    void retrieveUntil(const char* end) noexcept {
        assert(peek() <= end);
        assert(end <= beginWrite());
        retrieve(static_cast<std::size_t>(end - peek()));
    }

    void retrieveAll() noexcept {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    [[nodiscard]] std::string retrieveAllAsString() {
        return retrieveAsString(readableBytes());
    }

    [[nodiscard]] std::string retrieveAsString(std::size_t len) {
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    [[nodiscard]] std::string_view toStringPiece() const noexcept {
        return {peek(), readableBytes()};
    }

    void append(std::string_view str) {
        ensureWritableBytes(str.size());
        std::ranges::copy(str, beginWrite());
        hasWritten(str.size());
    }

    void append(const void* data, std::size_t len) {
        append(std::string_view{static_cast<const char*>(data), len});
    }

    void ensureWritableBytes(std::size_t len) {
        if (writableBytes() < len) {
            makeSpace(len);
        }
        assert(writableBytes() >= len);
    }

    [[nodiscard]] char* beginWrite() noexcept { return begin() + writerIndex_; }

    [[nodiscard]] const char* beginWrite() const noexcept {
        return begin() + writerIndex_;
    }

    void hasWritten(std::size_t len) noexcept {
        assert(len <= writableBytes());
        writerIndex_ += len;
    }

    void unwrite(std::size_t len) noexcept {
        assert(len <= readableBytes());
        writerIndex_ -= len;
    }

    template <std::integral T>
    void appendInt(T x) {
        if constexpr (std::endian::native == std::endian::little) {
            x = std::byteswap(x);  // C++23
        }
        append(&x, sizeof(x));
    }

    template <std::integral T>
    [[nodiscard]] T readInt() {
        T result = peekInt<T>();
        retrieve(sizeof(T));
        return result;
    }

    template <std::integral T>
    [[nodiscard]] T peekInt() const noexcept {
        assert(readableBytes() >= sizeof(T));
        T val = 0;
        std::memcpy(&val, peek(), sizeof(val));
        if constexpr (std::endian::native == std::endian::little) {
            return std::byteswap(val);  // C++23
        }
        return val;
    }

    template <std::integral T>
    void prependInt(T x) {
        if constexpr (std::endian::native == std::endian::little) {
            x = std::byteswap(x);
        }
        prepend(&x, sizeof(x));
    }

    void prepend(const void* data, std::size_t len) {
        assert(len <= prependableBytes());
        readerIndex_ -= len;
        const char* d = static_cast<const char*>(data);
        std::ranges::copy(std::span<const char>{d, len}, begin() + readerIndex_);
    }

    void shrink(std::size_t reserve) {
        Buffer other;
        other.ensureWritableBytes(readableBytes() + reserve);
        other.append(toStringPiece());
        swap(other);
        buffer_.shrink_to_fit();
    }

    [[nodiscard]] std::size_t internalCapacity() const noexcept {
        return buffer_.capacity();
    }

    /// Read data directly into buffer.
    ///
    /// It may implement with readv(2)
    /// @return result of read(2), @c errno is saved
    ssize_t readFd(int fd, int* savedErrno);

private:
    [[nodiscard]] char* begin() noexcept { return &*buffer_.begin(); }

    [[nodiscard]] const char* begin() const noexcept {
        return &*buffer_.begin();
    }

    // template <std::ranges::contiguous_range R>
    //     requires(std::is_same_v<std::ranges::range_value_t<R>, char> &&
    //              !std::is_same_v<R, std::string_view>)
    // void append(R data) {
    //     ensureWritableBytes(data.size());
    //     std::ranges::copy(data, beginWrite());
    //     hasWritten(data.size());
    // }

    void makeSpace(std::size_t len) {
        // 如果现有可写空间 + 可回收的前置空间仍不足以容纳 len字节（且还要保留
        // kCheapPrepend大小的头部），说明空间不够，需要扩容。
        if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
            buffer_.resize(writerIndex_ + len);
        } else {
            // 此时虽然总空间足够，但尾部空间不足，而头部有大量已读空间（prependableBytes）。
            std::size_t readable = readableBytes();
            // 将当前所有可读数据（从 readerIndex_到 writerIndex_）整体向前移动到 kCheapPrepend偏移处。
            std::ranges::copy(readableSpan(), begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
            assert(readable == readableBytes());
        }
    }

    std::vector<char> buffer_;
    std::size_t readerIndex_ = kCheapPrepend;
    std::size_t writerIndex_ = kCheapPrepend;
};
}  // namespace chaoxi::net