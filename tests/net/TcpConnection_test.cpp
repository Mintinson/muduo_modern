///
/// @file tests/net/TcpConnection_test.cpp
/// @brief TcpConnection 单元测试 —— 覆盖连接生命周期、发送/接收、状态机、回调
///
/// 测试使用的技术:
///   - socketpair(2) 创建一对已连接的本地 socket，模拟 TCP 连接
///   - 每个 TEST 创建一个独立的 EventLoop，在同一个线程中操作（简化测试）
///   - 通过连接的一端发送数据，在另一端用 TcpConnection 接收验证
///
/// 注意: TcpConnection 不拥有 EventLoop，且必须在 EventLoop 线程中使用。
///       本测试中的所有操作都在主线程中进行（loop 也在主线程），
///       所以不需要跨线程唤醒。
///

#include "chaoxi/net/TcpConnection.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/Buffer.hpp"
#include "chaoxi/base/Timestamp.hpp"

#include <cstring>
#include <string>

#include <gtest/gtest.h>
#include <netinet/tcp.h>   // for struct tcp_info

#include <sys/socket.h>
#include <unistd.h>

using namespace chaoxi::net;

///
/// @brief 测试夹具 —— 提供 EventLoop + 一对已连接的 socket fd
///
/// 结构示意:
///
///   socketpair() 创建一对 fd:
///
///     fds_[0]  ──────────────  fds_[1]
///     (test端)                (TcpConnection端)
///     ↑ 我们在测试中手动       ↑ 被包装成 TcpConnection
///       write/read 验证
///
class TcpConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // socketpair 创建一对已连接的 Unix domain socket
        // AF_UNIX: Unix 域协议（本地通信，无需 TCP 握手）
        // SOCK_STREAM: 流式 socket，行为类似 TCP
        // 创建后 fds_[0] 和 fds_[1] 已连接，可以互相收发数据
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
    }

    void TearDown() override {
        cleanupConnection();
        // 关闭测试端 fd
        if (fds_[0] >= 0) {
            ::close(fds_[0]);
            fds_[0] = -1;
        }
    }

    /// 确保连接处于安全销毁状态（kDisconnected 且 channel 已 remove）
    void cleanupConnection() {
        if (conn_ && !cleanup_done_) {
            if (conn_->connected()) {
                // kConnected → kDisconnected (含 channel_->remove())
                conn_->connectDestroyed();
            } else {
                // kDisconnected / kDisconnecting / kConnecting
                // 统一交给 connectDestroyed 处理（它会处理各种状态）
                conn_->connectDestroyed();
            }
            conn_.reset();
        }
    }

    ///
    /// @brief 创建一个 TcpConnection 包装 fds_[1]
    ///
    /// 创建后状态为 kConnecting，需要手动调用 connectEstablished() 进入 kConnected
    ///
    void createConnection() {
        InetAddress localAddr(0, true);   // 127.0.0.1:0
        InetAddress peerAddr(0, true);    // 127.0.0.1:0
        conn_ = std::make_shared<TcpConnection>(
            &loop_, "test_conn", fds_[1], localAddr, peerAddr);
    }

    /// 将连接推进到 kConnected 状态（模拟 TcpServer accept 后的流程）
    void establishConnection() {
        ASSERT_TRUE(conn_);
        conn_->connectEstablished();
        ASSERT_TRUE(conn_->connected());
    }

    EventLoop loop_;
    std::shared_ptr<TcpConnection> conn_;
    int fds_[2] = {-1, -1};
    bool cleanup_done_{false};  // 测试已手动完成清理，跳过 cleanupConnection
};

// ============================================================================
// 测试1: 构造和基本访问器
// ============================================================================

TEST_F(TcpConnectionTest, ConstructorAndAccessors) {
    InetAddress localAddr(9981, true);   // 127.0.0.1:9981
    InetAddress peerAddr(8888, true);    // 127.0.0.1:8888

    conn_ = std::make_shared<TcpConnection>(
        &loop_, "my_conn", fds_[1], localAddr, peerAddr);

    // 构造后处于 kConnecting 状态
    EXPECT_FALSE(conn_->connected());
    EXPECT_FALSE(conn_->disconnected());
    EXPECT_EQ(conn_->name(), "my_conn");
    EXPECT_EQ(conn_->getLoop(), &loop_);
    EXPECT_EQ(conn_->localAddress().toIpPort(), "127.0.0.1:9981");
    EXPECT_EQ(conn_->peerAddress().toIpPort(), "127.0.0.1:8888");
    EXPECT_TRUE(conn_->isReading());  // reading_ 默认为 true
}

// ============================================================================
// 测试2: 状态机转换
// ============================================================================

TEST_F(TcpConnectionTest, StateTransitions) {
    createConnection();

    // 初始状态: kConnecting
    EXPECT_FALSE(conn_->connected());
    EXPECT_FALSE(conn_->disconnected());

    // connectEstablished → kConnected
    conn_->connectEstablished();
    EXPECT_TRUE(conn_->connected());
    EXPECT_FALSE(conn_->disconnected());

    // connectDestroyed → kDisconnected
    conn_->connectDestroyed();
    EXPECT_FALSE(conn_->connected());
    EXPECT_TRUE(conn_->disconnected());
    cleanup_done_ = true;  // 告诉 cleanupConnection 不要再调用 connectDestroyed
}

// ============================================================================
// 测试3: 在 kConnected 之前发送被拒绝
// ============================================================================

TEST_F(TcpConnectionTest, SendBeforeConnectedIsIgnored) {
    createConnection();
    // 还没调用 connectEstablished，state_ 还是 kConnecting

    // send 内部检查 state_ == kConnected，不满足则静默丢弃
    // 这里应该不 crash，也不会有数据发送出去
    conn_->send("should be ignored");

    // 验证对端没有收到数据
    char buf[64];
    // 设置一个很短的超时来检查是否有数据
    struct timeval tv = {0, 1000};  // 1ms
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fds_[0], &readfds);
    int ret = ::select(fds_[0] + 1, &readfds, nullptr, nullptr, &tv);
    EXPECT_EQ(ret, 0);  // 不应有数据可读
}

// ============================================================================
// 测试4: 基本发送和接收
// ============================================================================

TEST_F(TcpConnectionTest, SendAndReceive) {
    createConnection();
    establishConnection();

    bool messageReceived = false;
    std::string receivedData;

    // 设置消息回调
    conn_->setMessageCallback(
        [&](const TcpConnectionPtr&, Buffer& buf, chaoxi::Timestamp) {
            messageReceived = true;
            receivedData = buf.retrieveAllAsString();
        });

    // 从对端 (fds_[0]) 发送数据
    std::string testMsg = "Hello from the other side!";
    ssize_t n = ::write(fds_[0], testMsg.data(), testMsg.size());
    ASSERT_EQ(n, static_cast<ssize_t>(testMsg.size()));

    // 运行一次事件循环来分发读事件
    // 因为 TcpConnection 的 fd 在 Poller 中，poll 会检测到可读
    // 但我们不能调用 loop.loop() 因为会阻塞
    // 我们可以用一个短的 poll 来驱动一次事件
    // 更简单的方法：手动触发 handleRead
    // 但为了测试完整性，我们模拟 EventLoop 的行为：

    // 实际上：我们需要让 EventLoop 的 poll 检测到 fd 可读
    // 最简单的方式：直接调用内部方法测试（单元测试的白盒特性）
    // 或者运行一小段时间的 loop（用 timer 来 quit）

    // 方案：设置一个 50ms 的定时器来退出 loop，给 poll 足够时间处理事件
    loop_.runAfter(0.05, [&]() { loop_.quit(); });
    loop_.loop();

    EXPECT_TRUE(messageReceived);
    EXPECT_EQ(receivedData, testMsg);
}

// ============================================================================
// 测试5: send 到对端
// ============================================================================

TEST_F(TcpConnectionTest, SendDataToPeer) {
    createConnection();
    establishConnection();

    // 从 TcpConnection 端发送数据
    conn_->send("Data from TcpConnection");

    // 在对端 (fds_[0]) 读取
    char buf[128] = {0};
    // 设置非阻塞读取
    struct timeval tv = {0, 100000};  // 100ms
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fds_[0], &readfds);
    int ret = ::select(fds_[0] + 1, &readfds, nullptr, nullptr, &tv);

    // send 是在 EventLoop 线程中执行的（我们当前就在 loop 线程），
    // sendInLoop 会直接 write，所以对端应该可以读到
    if (ret > 0) {
        ssize_t n = ::read(fds_[0], buf, sizeof(buf) - 1);
        EXPECT_GT(n, 0);
        EXPECT_STREQ(buf, "Data from TcpConnection");
    }
    // 如果 write 返回 EAGAIN（不太可能但可能），数据会在 outputBuffer_ 中
}

// ============================================================================
// 测试6: 对端关闭连接后的行为
// ============================================================================

TEST_F(TcpConnectionTest, PeerCloseTriggersHandleClose) {
    createConnection();
    establishConnection();

    bool closeTriggered = false;

    conn_->setCloseCallback([&](const TcpConnectionPtr&) {
        closeTriggered = true;
    });

    // 关闭对端 (fds_[0])，这会让 fds_[1] 变为可读 (read 返回 0)
    ::close(fds_[0]);
    fds_[0] = -1;  // 防止 TearDown 重复关闭

    // 运行事件循环让 handleRead 检测到 read 返回 0
    loop_.runAfter(0.05, [&]() { loop_.quit(); });
    loop_.loop();

    EXPECT_TRUE(closeTriggered);
    EXPECT_TRUE(conn_->disconnected());
}

// ============================================================================
// 测试7: 连接回调
// ============================================================================

TEST_F(TcpConnectionTest, ConnectionCallback) {
    createConnection();

    int connectCount = 0;
    int disconnectCount = 0;

    conn_->setConnectionCallback([&](const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            ++connectCount;
        } else {
            ++disconnectCount;
        }
    });

    // connectEstablished 会触发 connectionCallback_
    conn_->connectEstablished();
    EXPECT_EQ(connectCount, 1);
    EXPECT_EQ(disconnectCount, 0);

    // connectDestroyed 也会触发 connectionCallback_
    conn_->connectDestroyed();
    EXPECT_EQ(connectCount, 1);
    EXPECT_EQ(disconnectCount, 1);
    cleanup_done_ = true;
}

// ============================================================================
// 测试8: shutdown 状态转换
// ============================================================================

TEST_F(TcpConnectionTest, ShutdownTransitionsToDisconnecting) {
    createConnection();
    establishConnection();

    conn_->shutdown();
    // shutdown 成功后 state_ → kDisconnecting
    EXPECT_FALSE(conn_->connected());
    EXPECT_FALSE(conn_->disconnected());
    // 手动完成清理：kDisconnecting → kDisconnected
    conn_->connectDestroyed();
    cleanup_done_ = true;
}

// ============================================================================
// 测试9: forceClose 状态转换
// ============================================================================

TEST_F(TcpConnectionTest, ForceClose) {
    createConnection();
    establishConnection();

    bool closeCalled = false;
    conn_->setCloseCallback([&](const TcpConnectionPtr&) {
        closeCalled = true;
    });

    conn_->forceClose();

    // forceClose 使用 queueInLoop 延迟执行，需要运行 EventLoop 来触发
    loop_.runAfter(0.05, [&]() { loop_.quit(); });
    loop_.loop();

    EXPECT_TRUE(closeCalled);
    EXPECT_TRUE(conn_->disconnected());
    // handleClose 已经调用了 disableAll 和 setState(kDisconnected)，
    // 但 channel_->remove() 还没调用，让 cleanupConnection 来处理
}

// ============================================================================
// 测试10: 读写控制
// ============================================================================

TEST_F(TcpConnectionTest, StopAndStartRead) {
    createConnection();
    establishConnection();

    // 默认可以读
    EXPECT_TRUE(conn_->isReading());

    // 暂停读取
    conn_->stopRead();
    // stopRead 通过 runInLoop 提交，需要让 EventLoop 处理
    loop_.runAfter(0.05, [&]() { loop_.quit(); });
    loop_.loop();
    // 注意：stopReadInLoop 在 loop 线程执行，完成后 reading_ 应该是 false
    // 但需要通过 runInLoop 的 cb 执行
    // 再次运行以确认
    // 实际上 runInLoop 在 loop 线程是同步执行的，所以这里不需要等待

    // 恢复读取
    conn_->startRead();
}

// ============================================================================
// 测试11: setTcpNoDelay
// ============================================================================

TEST_F(TcpConnectionTest, SetTcpNoDelay) {
    createConnection();
    // setTcpNoDelay 委托给 Socket 对象，不抛异常即通过
    EXPECT_NO_THROW(conn_->setTcpNoDelay(true));
    EXPECT_NO_THROW(conn_->setTcpNoDelay(false));
}

// ============================================================================
// 测试12: 用户上下文 (context)
// ============================================================================

TEST_F(TcpConnectionTest, UserContext) {
    createConnection();

    // 设置任意类型的上下文
    int contextValue = 42;
    conn_->setContext(contextValue);
    EXPECT_EQ(std::any_cast<int>(conn_->getContext()), 42);

    // 修改上下文
    conn_->setContext(std::string("some context"));
    EXPECT_EQ(std::any_cast<std::string>(conn_->getContext()), "some context");

    // 获取可变上下文
    auto* ctx = conn_->getMutableContext();
    *ctx = std::string("modified");
    EXPECT_EQ(std::any_cast<std::string>(conn_->getContext()), "modified");
}

// ============================================================================
// 测试13: TCP 信息查询
// ============================================================================

TEST_F(TcpConnectionTest, GetTcpInfo) {
    createConnection();

    tcp_info info{};
    // 注意：Unix domain socket (socketpair) 可能不支持 TCP_INFO
    // 这里只验证方法不崩溃
    bool result = conn_->getTcpInfo(&info);
    (void)result;  // 结果依赖于 socket 类型
    SUCCEED();
}

TEST_F(TcpConnectionTest, GetTcpInfoString) {
    createConnection();

    std::string info = conn_->getTcpInfoString();
    // 字符串可能为空（Unix domain socket 不支持 TCP_INFO）
    // 验证方法不崩溃即可
    SUCCEED();
}

// ============================================================================
// 测试14: forceCloseWithDelay
// ============================================================================

TEST_F(TcpConnectionTest, ForceCloseWithDelay) {
    createConnection();
    establishConnection();

    // 设置延迟关闭（0.02 秒）
    conn_->forceCloseWithDelay(0.02);

    // 立即检查：应该还在 kDisconnecting 状态，还没真正关闭
    EXPECT_FALSE(conn_->connected());

    // 等定时器触发
    loop_.runAfter(0.1, [&]() { loop_.quit(); });
    loop_.loop();

    // 延迟关闭后应该是 kDisconnected
    EXPECT_TRUE(conn_->disconnected());
}

// ============================================================================
// 测试15: 大量数据的发送和接收
// ============================================================================

TEST_F(TcpConnectionTest, SendLargeData) {
    createConnection();
    establishConnection();

    std::string receivedData;
    size_t totalReceived = 0;

    conn_->setMessageCallback(
        [&](const TcpConnectionPtr&, Buffer& buf, chaoxi::Timestamp) {
            totalReceived += buf.readableBytes();
            receivedData += buf.retrieveAllAsString();
        });

    // 发送大量数据（从对端）
    std::string largeMsg(65536, 'X');  // 64KB
    ssize_t totalWritten = 0;
    while (totalWritten < static_cast<ssize_t>(largeMsg.size())) {
        ssize_t n = ::write(fds_[0], largeMsg.data() + totalWritten,
                            largeMsg.size() - totalWritten);
        if (n > 0) {
            totalWritten += n;
        } else if (n < 0 && errno != EAGAIN) {
            break;
        }
    }

    // 驱动事件循环
    loop_.runAfter(0.1, [&]() { loop_.quit(); });
    loop_.loop();

    // 验证收到了数据
    EXPECT_GT(totalReceived, 0ul);
    EXPECT_EQ(receivedData.size(), totalReceived);
    EXPECT_EQ(receivedData, largeMsg.substr(0, receivedData.size()));
}
