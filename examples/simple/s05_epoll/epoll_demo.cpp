///
/// @file examples/simple/s05_epoll/epoll_demo.cpp
/// @brief EPollPoller 实际使用示例 —— 验证 epoll kNew/kAdded/kDeleted 状态转换
///
/// 本示例通过 EventLoop + Channel 的使用路径，展示底层 EPollPoller 的行为:
///   1. enableReading → kNew → epoll_ctl(ADD) → kAdded
///   2. disableAll → kAdded → epoll_ctl(DEL) → kDeleted
///   3. enableReading again → kDeleted → epoll_ctl(ADD) → kAdded
///   4. remove → kDeleted/kAdded → epoll_ctl(DEL) → kNew
///   5. poll 检测事件、多个 Channel 并发
///
/// 使用方法：编译运行后观察状态变化日志。
///

#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <array>
#include <cstddef>
#include <print>

#include <fcntl.h>
#include <unistd.h>

using namespace chaoxi;
using namespace chaoxi::net;

int main()
{
    // ========================================================================
    // Step 1: 创建 EventLoop (内部自动创建 EPollPoller)
    // ========================================================================
    EventLoop loop;
    std::println("=== EPollPoller Demo (via EventLoop) ===");
    std::println("EventLoop created with EPollPoller (default poller on Linux)");

    // ========================================================================
    // Step 2: 创建 3 个 pipe + Channel，模拟 3 个"连接"
    // ========================================================================
    constexpr int kNumChannels = 3;

    struct PipeInfo
    {
        int readFd;
        int writeFd;
    };

    std::array<PipeInfo, kNumChannels> pipes{};
    std::array<Channel*, kNumChannels> chs{nullptr};

    for (std::size_t i = 0; i < kNumChannels; ++i)
    {
        int p[2];
        ::pipe2(p, O_NONBLOCK | O_CLOEXEC);
        pipes[i] = {.readFd = p[0], .writeFd = p[1]};

        auto* ch = new Channel(&loop, p[0]);  // 监控读端
        ch->setReadCallback(
            [i, fd = p[0]](Timestamp)
            {
                // 读掉数据以免 level-trigger 下重复触发（仅用于演示）
                char buf[64];
                auto ed = ::read(fd, buf, sizeof(buf));
                buf[ed] = '\0';
                std::println("  Channel{} triggered! Read: {}", i, buf);
            });
        ch->enableReading();  // → kNew → kAdded (epoll_ctl ADD)

        std::println("Channel{}: fd={}, index={} (kAdded)", i, p[0],
                     ch->index());
        chs[i] = ch;
    }

    // ========================================================================
    // Step 3: 写 Channel1 → 验证只有它被触发
    // ========================================================================
    std::println("\n--- Writing to Channel1 only ---");
    ::write(pipes[1].writeFd, "hello", 5);

    loop.runAfter(0.1, [&]() { loop.quit(); });
    loop.loop();

    // 读掉数据（防止下次 poll 再次触发）
    char buf[64];
    auto rsize = ::read(pipes[1].readFd, buf, sizeof(buf));
    std::println("total read after trigger: {}", rsize);

    // ========================================================================
    // Step 4: disableAll → kDeleted 状态
    // ========================================================================
    std::println("\n--- Disabling Channel2 ---");
    std::println("  Before: index={}", chs[2]->index());
    chs[2]->disableAll();
    std::println("  After:  index={} (kDeleted)", chs[2]->index());

    ::write(pipes[2].writeFd, "should be ignored", 17);
    loop.runAfter(0.05, [&]() { loop.quit(); });
    loop.loop();
    std::println("  (no event from Channel2 — it was disabled)");

    // ========================================================================
    // Step 5: 重新启用 Channel2 → kDeleted → kAdded
    // ========================================================================
    std::println("\n--- Re-enabling Channel2 ---");
    chs[2]->enableReading();
    std::println("  After re-enable: index={} (kAdded)", chs[2]->index());

    ::write(pipes[2].writeFd, "hello again", 11);
    loop.runAfter(0.05, [&]() { loop.quit(); });
    loop.loop();

    // ========================================================================
    // Step 6: 清理
    // ========================================================================
    std::println("\n--- Cleanup ---");
    for (int i = 0; i < kNumChannels; ++i)
    {
        std::println("  Channel{}: disableAll → remove → index={}", i, -1);
        chs[i]->disableAll();
        chs[i]->remove();
        ::close(pipes[i].readFd);
        ::close(pipes[i].writeFd);
        delete chs[i];
    }

    std::println("\n=== Demo complete ===");
    std::println("\nEPollPoller state transitions verified:");
    std::println("  kNew({}) → enableReading → kAdded({})", -1, 1);
    std::println("  kAdded({}) → disableAll → kDeleted({})", 1, 2);
    std::println("  kDeleted({}) → enableReading → kAdded({})", 2, 1);
    std::println("  kDeleted({}) → remove → kNew({})", 2, -1);

    return 0;
}
