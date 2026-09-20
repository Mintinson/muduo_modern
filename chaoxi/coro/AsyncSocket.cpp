#include "chaoxi/coro/AsyncSocket.hpp"

#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <cerrno>
#include <system_error>
#include <utility>

// 平台相关的 TCP 选项头文件：
//   - Windows 下使用 mstcpip.h 中的 TCP_NODELAY 等定义；
//   - POSIX 下使用 netinet/tcp.h。
#ifdef _WIN32
    #include <mstcpip.h>
#else
    #include <netinet/tcp.h>
#endif

namespace chaoxi::coro
{

/**
 * @brief 从已有 fd 构造 AsyncSocket。
 *
 * 注意 FdOwnership::owned 表示 AsyncSocket 接管 fd 的所有权，
 * 析构或 close 时会关闭它。如果调用者不希望 fd 被关闭，
 * 应该使用 AsyncFd 的 borrowed 语义（本类暂未暴露该选项，
 * 如需借用可直接持有 AsyncFd）。
 *
 * 必须在 loop 线程调用（AsyncFd 构造函数会 assertInLoopThread）。
 */
AsyncSocket::AsyncSocket(net::EventLoop& loop, net::SocketHandle fd)
    : fd_(loop, fd, FdOwnership::owned)
{
}

/// 默认析构：AsyncFd 的析构会调用 close()，释放 fd 和 Channel。
AsyncSocket::~AsyncSocket() = default;

/**
 * @brief 公开的异步 connect 接口。
 *
 * 本身不含 co_await，只是返回 connectImpl 创建的惰性 Task。
 * 这样调用方可以决定何时 co_await，符合 Task 的惰性语义。
 */
Task<AsyncSocket> AsyncSocket::connect(net::EventLoop& loop,
                                       const net::InetAddress& address)
{
    return connectImpl(loop, address);
}

/**
 * @brief 异步 connect 的实现。
 *
 * 非阻塞 connect 的关键流程：
 *
 *   1. 创建非阻塞 socket。
 *   2. 调用 connect()：
 *      - 返回 0：立即连接成功（例如本地回环、已缓存路由）。
 *      - 返回 -1 且 errno == EINPROGRESS：连接正在进行中，需要等待可写事件。
 *      - 返回 -1 且 errno == EINTR：被信号中断，同样等待可写事件后检查 SO_ERROR。
 *      - 其他 errno：直接抛出。
 *   3. co_await waitWritable()：等待 fd 可写。可写意味着：
 *      - 连接成功；或
 *      - 连接失败（此时可写事件仍会触发，需要通过 SO_ERROR 查询具体错误）。
 *   4. 调用 getsockopt(SO_ERROR)：
 *      - 返回 0：连接成功。
 *      - 非 0：连接失败，抛出该错误。
 *
 * 为什么要捕获 waitWritable 的异常？
 *   - waitWritable 可能因 close 或其他原因抛出（例如 operation_canceled）。
 *   - 此时 SO_ERROR 可能仍为 0（连接其实已成功），但等待本身被取消。
 *   - 因此先检查 SO_ERROR，若为 0 再抛出等待期间记录的错误，
 *     避免把已成功的连接误判为失败。
 *
 * 参数用值传递 InetAddress：
 *   - 协程可能在 connectImpl 挂起后仍然存活；
 *   - 若用 const&，引用的对象可能在挂起期间被销毁；
 *   - 值传递让协程帧持有独立副本，安全。
 */
Task<AsyncSocket> AsyncSocket::connectImpl(net::EventLoop& loop,
                                           net::InetAddress address)
{
    // 协程体必须在 loop 线程执行，因为要操作 Channel / epoll。
    loop.assertInLoopThread();

    // 1. 创建非阻塞 socket。
    const net::SocketHandle fd =
        net::sockets::createNonblocking(address.family());
    if (fd == net::kInvalidSocket)
    {
        throw std::system_error(errno, std::generic_category());
    }

    // 2. 用 RAII 包装 fd：如果后续抛异常，AsyncSocket 析构会关闭 fd。
    AsyncSocket socket(loop, fd);

    // 3. 发起非阻塞 connect。
    const int result = net::sockets::connect(fd, address.getSockAddr());
    if (result < 0)
    {
        // 非阻塞 connect 的“正常”返回值：
        //   - EINPROGRESS：连接正在进行；
        //   - EINTR：被信号中断，但连接可能仍在进行。
        // 其他 errno 表示立即失败。
        if (errno != EINPROGRESS && errno != EINTR)
        {
            throw std::system_error(errno, std::generic_category());
        }

        // 4. 等待可写。注意：可写只表示“连接状态已确定”，
        //    成功还是失败需要查 SO_ERROR。
        std::error_code readinessError;
        try
        {
            co_await socket.fd_.waitWritable();
        }
        catch (const std::system_error& error)
        {
            // 等待被取消或出错，先记录，稍后判断。
            readinessError = error.code();
        }

        // 5. 查询 SO_ERROR，确定连接结果。
        const int error = net::sockets::getSocketError(fd);
        if (error != 0)
        {
            // 连接失败：以 SO_ERROR 为准，抛具体错误。
            throw std::system_error(error, std::generic_category());
        }

        // 6. SO_ERROR 为 0，说明连接成功。
        //    但如果等待期间有错误（例如被取消），则需要抛出该错误。
        if (readinessError)
        {
            throw std::system_error(readinessError);
        }
    }

    // 7. 连接成功，把 socket 移出返回。
    //    移动后局部 socket 变空，析构不会关闭 fd。
    co_return std::move(socket);
}

/**
 * @brief 半关闭写方向。
 *
 * shutdown(SHUT_WR) 会发送 FIN，对端读到 EOF，但本地仍可继续读。
 * 常见于请求-响应协议：客户端发完请求后 shutdownWrite，
 * 然后读取服务端响应直到 EOF。
 *
 * ENOTCONN 表示对端已断开连接，此时视为幂等成功，不抛异常。
 * 其他错误抛 std::system_error。
 *
 * 必须在 loop 线程调用（操作 fd）。
 */
void AsyncSocket::
    shutdownWrite()  // NOLINT(readability-make-member-function-const)
{
    if (net::sockets::shutdownWrite(nativeHandle()) < 0 && errno != ENOTCONN)
    {
        throw std::system_error(errno, std::generic_category());
    }
}

/**
 * @brief 设置 TCP_NODELAY。
 *
 * Nagle 算法会合并小包以减少网络拥塞，但增加延迟。
 * 对延迟敏感的应用（例如游戏、RPC、交互式协议）通常禁用 Nagle。
 *
 * 必须在 loop 线程调用。
 */
void AsyncSocket::
    setTcpNoDelay(  // NOLINT(readability-make-member-function-const)
        bool enabled)
{
    const int value = enabled ? 1 : 0;
    if (net::sockets::setSocketOption(nativeHandle(), IPPROTO_TCP, TCP_NODELAY,
                                      value) < 0)
    {
        throw std::system_error(errno, std::generic_category());
    }
}

/**
 * @brief 设置 SO_KEEPALIVE。
 *
 * 启用后，TCP 会定期发送 keepalive 探测包，检测对端是否存活。
 * 对长连接场景（例如 IM、推送、数据库连接池）非常有用。
 *
 * 注意：默认的 keepalive 间隔通常很长（2 小时），
 * 生产环境通常还需要设置 TCP_KEEPIDLE / TCP_KEEPINTVL / TCP_KEEPCNT。
 * 本接口只提供开关，具体参数可按需扩展。
 *
 * 必须在 loop 线程调用。
 */
void AsyncSocket::
    setKeepAlive(  // NOLINT(readability-make-member-function-const)
        bool enabled)
{
    const int value = enabled ? 1 : 0;
    if (net::sockets::setSocketOption(nativeHandle(), SOL_SOCKET, SO_KEEPALIVE,
                                      value) < 0)
    {
        throw std::system_error(errno, std::generic_category());
    }
}

}  // namespace chaoxi::coro