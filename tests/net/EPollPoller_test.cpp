///
/// @file tests/net/EPollPoller_test.cpp
/// @brief EPollPoller 单元测试 —— 通过 EventLoop + Channel 测试 epoll 行为
///
/// EPollPoller 现在是默认 Poller (DefaultPoller.cpp →
/// EPollPoller)，所以每个 EventLoop 都天然使用 epoll。 测试通过 Channel 的公开
/// API (enableReading, disableAll, remove) 验证 EPollPoller 的完整行为。 *
///
/// 测试覆盖:
///   - Channel kNew → kAdded → kDeleted → kNew 状态转换
///   - epoll_wait 检测可读事件
///   - epoll_ctl ADD/MOD/DEL 操作正确性
///   - 多 fd 并发监控
///   - 事件数组自动扩容
///

#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <memory>
#include <set>
#include <vector>

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

using namespace chaoxi;
using namespace chaoxi::net;

class EPollPollerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // EventLoop 构造时自动创建 EPollPoller (DefaultPoller)
    }

    void TearDown() override {
        for (int fd : extraFds_) {
            ::close(fd);
        }
    }

    /// 创建一个新 pipe 和对应的 Channel（自动注册到 EventLoop）
    std::pair<int, Channel*> makeChannel() {
        int p[2];
        ::pipe2(p, O_NONBLOCK | O_CLOEXEC);
        auto ch = std::make_unique<Channel>(&loop_, p[0]);  // 读端
        Channel* raw = ch.get();
        channels_.push_back(std::move(ch));
        extraFds_.push_back(p[1]);  // 写端
        return {p[1], raw};
    }

    EventLoop loop_;
    std::vector<std::unique_ptr<Channel>> channels_;
    std::vector<int> extraFds_;

    // EPollPoller 内部使用的 Channel index 常量
    static constexpr int kNew = -1;
    static constexpr int kAdded = 1;
    static constexpr int kDeleted = 2;
};

// ============================================================================
// 测试1: enableReading → kNew → kAdded
// ============================================================================

TEST_F(EPollPollerTest, EnableReadingRegistersInEpoll) {
    int p[2];
    ASSERT_EQ(::pipe2(p, O_NONBLOCK | O_CLOEXEC), 0);
    Channel ch(&loop_, p[0]);

    EXPECT_EQ(ch.index(), kNew);          // 初始状态
    EXPECT_EQ(ch.events(), 0);             // 无事件

    ch.enableReading();
    // enableReading 调用了 update() → EventLoop::updateChannel → Poller::updateChannel
    EXPECT_EQ(ch.index(), kAdded);         // 已注册到 epoll
    EXPECT_NE(ch.events(), 0);             // 有关注的事件

    // 清理
    ch.disableAll();
    ch.remove();
    ::close(p[0]);
    ::close(p[1]);
}

// ============================================================================
// 测试2: 检测可读事件
// ============================================================================

TEST_F(EPollPollerTest, DetectReadableEvent) {
    int p[2];
    ASSERT_EQ(::pipe2(p, O_NONBLOCK | O_CLOEXEC), 0);
    Channel ch(&loop_, p[0]);

    int readCount = 0;
    ch.setReadCallback([&](Timestamp) { ++readCount; });
    ch.enableReading();  // → kAdded

    // 写入触发可读
    ::write(p[1], "hello", 5);

    // 运行一次事件循环迭代来检测事件
    // 我们使用 runAfter 来在 50ms 后 quit
    loop_.runAfter(0.05, [&]() { loop_.quit(); });
    loop_.loop();

    EXPECT_GE(readCount, 1);

    ch.disableAll();
    ch.remove();
    ::close(p[0]);
    ::close(p[1]);
}

// ============================================================================
// 测试3: disableAll → kAdded → kDeleted
// ============================================================================

TEST_F(EPollPollerTest, DisableAllRemovesFromEpoll) {
    int p[2];
    ASSERT_EQ(::pipe2(p, O_NONBLOCK | O_CLOEXEC), 0);
    Channel ch(&loop_, p[0]);

    ch.enableReading();
    EXPECT_EQ(ch.index(), kAdded);

    // disableAll → events = 0 → epoll_ctl(DEL) → kDeleted
    ch.disableAll();
    EXPECT_EQ(ch.index(), kDeleted);
    EXPECT_TRUE(ch.isNoneEvent());

    // 写数据不应触发事件
    ::write(p[1], "x", 1);

    int readCount = 0;
    ch.setReadCallback([&](Timestamp) { ++readCount; });
    loop_.runAfter(0.05, [&]() { loop_.quit(); });
    loop_.loop();
    EXPECT_EQ(readCount, 0);  // disableAll 后不应收到事件

    ch.remove();
    ::close(p[0]);
    ::close(p[1]);
}

// ============================================================================
// 测试4: 重新启用 (kDeleted → kAdded)
// ============================================================================

TEST_F(EPollPollerTest, ReEnableAfterDisable) {
    int p[2];
    ASSERT_EQ(::pipe2(p, O_NONBLOCK | O_CLOEXEC), 0);
    Channel ch(&loop_, p[0]);

    ch.enableReading();
    EXPECT_EQ(ch.index(), kAdded);

    ch.disableAll();
    EXPECT_EQ(ch.index(), kDeleted);

    // 重新启用 → kDeleted → ADD → kAdded
    ch.enableReading();
    EXPECT_EQ(ch.index(), kAdded);

    // 验证能检测到事件
    int readCount = 0;
    ch.setReadCallback([&](Timestamp) { ++readCount; });
    ::write(p[1], "re", 2);

    loop_.runAfter(0.05, [&]() { loop_.quit(); });
    loop_.loop();
    EXPECT_GE(readCount, 1);

    ch.disableAll();
    ch.remove();
    ::close(p[0]);
    ::close(p[1]);
}

// ============================================================================
// 测试5: removeChannel 完全清理
// ============================================================================

TEST_F(EPollPollerTest, RemoveChannelFullCleanup) {
    int p[2];
    ASSERT_EQ(::pipe2(p, O_NONBLOCK | O_CLOEXEC), 0);
    Channel ch(&loop_, p[0]);

    ch.enableReading();
    EXPECT_TRUE(loop_.hasChannel(&ch));

    ch.disableAll();              // 先删除 epoll 注册
    ch.remove();     // 再从 map 中移除
    EXPECT_FALSE(loop_.hasChannel(&ch));
    EXPECT_EQ(ch.index(), kNew);

    ::close(p[0]);
    ::close(p[1]);
}

// ============================================================================
// 测试6: 多个 Channel 同时监控
// ============================================================================

TEST_F(EPollPollerTest, MultipleChannels) {
    auto [w1, ch1] = makeChannel();
    auto [w2, ch2] = makeChannel();

    int count1 = 0, count2 = 0;
    ch1->setReadCallback([&](Timestamp) { ++count1; });
    ch2->setReadCallback([&](Timestamp) { ++count2; });

    ch1->enableReading();
    ch2->enableReading();

    // 只触发 ch2
    ::write(w2, "b", 1);

    loop_.runAfter(0.05, [&]() { loop_.quit(); });
    loop_.loop();

    EXPECT_EQ(count1, 0);   // ch1 不应有事件
    EXPECT_GE(count2, 1);   // ch2 应该有事件

    ch1->disableAll();
    ch2->disableAll();
    ch1->remove();
    ch2->remove();
}

// ============================================================================
// 测试7: epoll 事件数组自动扩容 (超过 kInitEventListSize=16)
// ============================================================================

TEST_F(EPollPollerTest, EventArrayAutoExpansion) {
    constexpr int N = 20;  // > kInitEventListSize(16)
    std::vector<int> writeEnds;
    std::vector<Channel*> chs;

    for (int i = 0; i < N; ++i) {
        auto [w, ch] = makeChannel();
        ch->enableReading();
        writeEnds.push_back(w);
        chs.push_back(ch);
    }

    // 触发所有 fd 的可读事件
    for (int w : writeEnds) {
        ::write(w, "x", 1);
    }

    // 用 set 记录触发过的 fd（去重，只关心是否被触发过）
    std::set<int> triggeredFds;
    for (auto* ch : chs) {
        ch->setReadCallback([&, fd = ch->fd()](Timestamp) {
            triggeredFds.insert(fd);
            char buf[64];
            ::read(fd, buf, sizeof(buf));  // 清除 level-trigger 数据
        });
    }

    loop_.runAfter(0.1, [&]() { loop_.quit(); });
    loop_.loop();

    // 所有 N 个 fd 至少被触发一次
    EXPECT_EQ(triggeredFds.size(), static_cast<size_t>(N));

    for (auto* ch : chs) {
        ch->disableAll();
        ch->remove();
    }
}

// ============================================================================
// 测试8: 写事件检测
// ============================================================================

TEST_F(EPollPollerTest, WriteEventDetection) {
    int p[2];
    ASSERT_EQ(::pipe2(p, O_NONBLOCK | O_CLOEXEC), 0);
    Channel ch(&loop_, p[1]);  // 监控写端

    int writeCount = 0;
    ch.setWriteCallback([&] { ++writeCount; });
    ch.enableWriting();  // 关注写事件

    // pipe 写端通常立即可写
    loop_.runAfter(0.05, [&]() { loop_.quit(); });
    loop_.loop();

    EXPECT_GE(writeCount, 1);

    ch.disableAll();
    ch.remove();
    ::close(p[0]);
    ::close(p[1]);
}

// ============================================================================
// 测试9: EPOLLHUP (对端关闭)
// ============================================================================

TEST_F(EPollPollerTest, HangupDetection) {
    int p[2];
    ASSERT_EQ(::pipe2(p, O_NONBLOCK | O_CLOEXEC), 0);
    Channel ch(&loop_, p[0]);

    int closeCount = 0;
    ch.setCloseCallback([&] {
        ++closeCount;
        loop_.quit();  // 收到 HUP 后立即退出
    });
    ch.enableReading();

    // 关闭写端 → 读端收到 POLLHUP
    ::close(p[1]);

    // 不需要额外的 timer quit，HUP 回调中会 quit
    loop_.runAfter(1.0, [&]() { loop_.quit(); });  // 安全兜底
    loop_.loop();

    // POLLHUP 会被检测到
    EXPECT_EQ(closeCount, 1);

    ch.disableAll();
    ch.remove();
    ::close(p[0]);
}
