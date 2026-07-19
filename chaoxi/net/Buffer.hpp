#pragma once

///
/// @file Buffer.hpp
/// @brief 网络缓冲区 —— 支持前置空间、自动扩容、readv 零拷贝读取
///
/// ╔══════════════════════════════════════════════════════════════════════╗
/// ║                  Buffer 内存布局（环形逻辑）                          ║
/// ╠══════════════════════════════════════════════════════════════════════╣
/// ║                                                                      ║
/// ║   ┌───────┬──────────────────┬──────────────────────┬──────┐         ║
/// ║   │ prepend │   readable       │   writable            │      │         ║
/// ║   │ (已用) │   (待读取)       │   (可写入)            │      │         ║
/// ║   └───────┴──────────────────┴──────────────────────┴──────┘         ║
/// ║   ↑          ↑                ↑                       ↑              ║
/// ║  begin()  readerIndex_    writerIndex_           buffer_.size()   ║
/// ║                                                                      ║
/// ║  - prependableBytes = readerIndex_ - kCheapPrepend(8)                ║
/// ║  - readableBytes    = writerIndex_ - readerIndex_                   ║
/// ║  - writableBytes    = buffer_.size() - writerIndex_                 ║
/// ║                                                                      ║
/// ║  自动扩容策略：                                                       ║
/// ║    ① 尾部空间不足但头部空闲空间够 → 数据前移（不需要新分配）        ║
/// ║    ② 总空间不够 → 重新分配更大的 vector                               ║
/// ║                                                                      ║
/// ╚══════════════════════════════════════════════════════════════════════╝
///

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chaoxi::net
{

///
/// @brief 非连续收发专用的缓冲区（设计参考 muduo Buffer）
///
/// 特点：
///   - 前置 8 字节 prependable 空间，可用于在数据前添加头部（如长度字段）
///   - 自动扩容：writeable 不足时自动腾挪或重分配
///   - readFd 使用 readv(2)，栈上 64KB extrabuf 兜底，避免 buffer 频繁扩容
///   - 直接操作 std::vector<char>，连续内存，缓存友好
///
class Buffer
{
public:
    static constexpr std::size_t kCheapPrepend = 8;    ///< 前置空间，用于协议头
    static constexpr std::size_t kInitialSize = 1024;  ///< 初始缓冲区大小
    static constexpr std::string_view kCRLF = "\r\n";  ///< HTTP CRLF 分隔符

    explicit Buffer(std::size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize)
    {
        assert(readableBytes() == 0);
        assert(writableBytes() == initialSize);
        assert(prependableBytes() == kCheapPrepend);
    }

    void swap(Buffer& rhs) noexcept
    {
        buffer_.swap(rhs.buffer_);
        std::swap(readerIndex_, rhs.readerIndex_);
        std::swap(writerIndex_, rhs.writerIndex_);
    }

    // ---- 长度查询 ----

    /// 可读数据字节数
    [[nodiscard]] std::size_t readableBytes() const noexcept
    {
        return writerIndex_ - readerIndex_;
    }

    /// 可写空间字节数
    [[nodiscard]] std::size_t writableBytes() const noexcept
    {
        return buffer_.size() - writerIndex_;
    }

    /// 前置空间字节数（可用于 prepend 或在扩容时复用）
    [[nodiscard]] std::size_t prependableBytes() const noexcept
    {
        return readerIndex_;
    }

    // ---- 数据访问 ----

    /// 可读数据起始指针
    [[nodiscard]] const char* peek() const noexcept
    {
        return begin() + readerIndex_;
    }

    /// 可读数据的 span 视图
    [[nodiscard]] std::span<const char> readableSpan() const noexcept
    {
        return {peek(), readableBytes()};
    }

    /// 可写空间的 span 视图
    [[nodiscard]] std::span<char> writableSpan() noexcept
    {
        return {beginWrite(), writableBytes()};
    }

    // ---- CRLF / EOL 搜索（HTTP 协议使用） ----

    /// 在可读数据中搜索 "\r\n"
    [[nodiscard]] const char* findCRLF() const noexcept
    {
        auto span = readableSpan();
        auto it = std::ranges::search(span, kCRLF).begin();
        return it == span.end() ? nullptr : &*it;
    }

    /// 从 start 位置开始搜索 "\r\n"
    [[nodiscard]] const char* findCRLF(const char* start) const noexcept
    {
        assert(peek() <= start);
        assert(start <= beginWrite());
        auto span = std::span<const char>{
            start, static_cast<std::size_t>(beginWrite() - start)};
        auto it = std::ranges::search(span, kCRLF).begin();
        return it == span.end() ? nullptr : &*it;
    }

    /// 在可读数据中搜索 '\n'
    [[nodiscard]] const char* findEOL() const noexcept
    {
        const void* eol = std::memchr(peek(), '\n', readableBytes());
        return static_cast<const char*>(eol);
    }

    /// 从 start 位置开始搜索 '\n'
    [[nodiscard]] const char* findEOL(const char* start) const noexcept
    {
        assert(peek() <= start);
        assert(start <= beginWrite());
        const void* eol = std::memchr(
            start, '\n',
            static_cast<std::size_t>(std::distance(start, beginWrite())));
        return static_cast<const char*>(eol);
    }

    // ---- 消费数据 ----

    /// 丢弃前 len 字节可读数据（readerIndex_ 前移）
    void retrieve(std::size_t len) noexcept
    {
        assert(len <= readableBytes());
        if (len < readableBytes())
        {
            readerIndex_ += len;
        }
        else
        {
            retrieveAll();
        }
    }

    /// 丢弃直到 end 指针位置的数据
    void retrieveUntil(const char* end) noexcept
    {
        assert(peek() <= end);
        assert(end <= beginWrite());
        retrieve(static_cast<std::size_t>(end - peek()));
    }

    /// 丢弃所有可读数据（重置 reader/writer 索引）
    void retrieveAll() noexcept
    {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    /// 取出所有可读数据为 string（会消费数据）
    [[nodiscard]] std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());
    }

    /// 取出前 len 字节为 string
    [[nodiscard]] std::string retrieveAsString(std::size_t len)
    {
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    /// 查看可读数据的 string_view（不消费数据）
    [[nodiscard]] std::string_view toStringPiece() const noexcept
    {
        return {peek(), readableBytes()};
    }

    // ---- 写入数据 ----

    /// 追加 string_view 到写位置
    void append(std::string_view str)
    {
        ensureWritableBytes(str.size());
        std::ranges::copy(str, beginWrite());
        hasWritten(str.size());
    }

    /// 追加裸数据
    void append(const void* data, std::size_t len)
    {
        append(std::string_view{static_cast<const char*>(data), len});
    }

    /// 确保有 len 字节的可写空间（自动扩容）
    void ensureWritableBytes(std::size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len);
        }
        assert(writableBytes() >= len);
    }

    /// 可写起始位置
    [[nodiscard]] char* beginWrite() noexcept { return begin() + writerIndex_; }

    [[nodiscard]] const char* beginWrite() const noexcept
    {
        return begin() + writerIndex_;
    }

    /// 标记 writerIndex 前移 len 字节（写入完成后调用）
    void hasWritten(std::size_t len) noexcept
    {
        assert(len <= writableBytes());
        writerIndex_ += len;
    }

    /// 回退 writerIndex（撤销上一次 hasWritten）
    void unwrite(std::size_t len) noexcept
    {
        assert(len <= readableBytes());
        writerIndex_ -= len;
    }

    // ---- 整数类型网络字节序读写 ----

    /// 追加整数（网络字节序，大端）
    template <std::integral T>
    void appendInt(T x)
    {
        if constexpr (std::endian::native == std::endian::little)
        {
            x = std::byteswap(x);  // C++23
        }
        append(&x, sizeof(x));
    }

    /// 读取整数并消费数据
    template <std::integral T>
    [[nodiscard]] T readInt()
    {
        T result = peekInt<T>();
        retrieve(sizeof(T));
        return result;
    }

    /// 查看整数但不消费数据
    template <std::integral T>
    [[nodiscard]] T peekInt() const noexcept
    {
        assert(readableBytes() >= sizeof(T));
        T val = 0;
        std::memcpy(&val, peek(), sizeof(val));
        if constexpr (std::endian::native == std::endian::little)
        {
            return std::byteswap(val);
        }
        return val;
    }

    /// 在 prependable 空间写入整数（用于在数据前加协议头）
    template <std::integral T>
    void prependInt(T x)
    {
        if constexpr (std::endian::native == std::endian::little)
        {
            x = std::byteswap(x);
        }
        prepend(&x, sizeof(x));
    }

    /// 在 prependable 空间写入数据
    void prepend(const void* data, std::size_t len)
    {
        assert(len <= prependableBytes());
        readerIndex_ -= len;
        const char* d = static_cast<const char*>(data);
        std::ranges::copy(std::span<const char>{d, len}, begin() + readerIndex_);
    }

    // ---- 容量管理 ----

    /// 收缩到刚好容纳当前数据 + reserve 的空间
    void shrink(std::size_t reserve)
    {
        Buffer other;
        other.ensureWritableBytes(readableBytes() + reserve);
        other.append(toStringPiece());
        swap(other);
        buffer_.shrink_to_fit();
    }

    /// 当前 vector 的 capacity（用于诊断）
    [[nodiscard]] std::size_t internalCapacity() const noexcept
    {
        return buffer_.capacity();
    }

    /// 从 fd 读取数据到缓冲区（使用 readv，栈上 64KB 兜底）
    /// 具体做法是，在栈上准备一个65536字节的 extrabuf，然后利用 readv()
    /// 来读取数据，iovec 有两块，第一块指向 Buffer 中的 writable
    /// 字节，另一块指向栈上的 extrabuf。这样如果读入的数据不多，
    // 那么全部都读到Buffer中去了；如果长度超过 Buffer 的 writable
    // 字节数，就会读到栈上的 extrabuf 里， 然后程序再把 extrabuf 里的数据
    // append() 到 Buffer 中
    // 这么做利用了临时栈上空间 14
    // ，避免每个连接的初始Buffer过大造成的内存浪费，也避免反复调用read()的系统开销
    /// @return read 返回的字节数，失败返回 -1 并设置 *savedErrno
    ssize_t readFd(int fd, int* savedErrno);

private:
    [[nodiscard]] char* begin() noexcept { return &*buffer_.begin(); }

    [[nodiscard]] const char* begin() const noexcept
    {
        return &*buffer_.begin();
    }

    /// 扩容逻辑：优先前移数据复用空间，不够才重新分配
    void makeSpace(std::size_t len)
    {
        if (writableBytes() + prependableBytes() < len + kCheapPrepend)
        {
            buffer_.resize(writerIndex_ + len);
        }
        else
        {
            std::size_t readable = readableBytes();
            std::ranges::copy(readableSpan(), begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
            assert(readable == readableBytes());
        }
    }

    std::vector<char> buffer_;
    std::size_t readerIndex_ = kCheapPrepend;  ///< 可读数据起始偏移
    std::size_t writerIndex_ = kCheapPrepend;  ///< 可写空间起始偏移
};

}  // namespace chaoxi::net
