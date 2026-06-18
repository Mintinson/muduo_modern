#include <gtest/gtest.h>
#include "chaoxi/net/EventLoopThreadPool.hpp"
#include "chaoxi/net/EventLoop.hpp"

#include <stdio.h>
#include <unistd.h>

using namespace chaoxi;
using namespace chaoxi::net;

// 辅助打印函数
void print(EventLoop* p = NULL)
{
    // printf("main(): pid = %d, tid = %d, loop = %p\n",
    //        getpid(), CurrentThread::tid(), p);
}

void init(EventLoop* p)
{
    // printf("init(): pid = %d, tid = %d, loop = %p\n",
    //        getpid(), CurrentThread::tid(), p);
}

// 建立一个测试夹具 (Test Fixture)
// 这能让每个测试用例都拥有一个独立的、干净的 EventLoop 对象
class EventLoopThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个 TEST_F 运行前调用
    }

    void TearDown() override {
        // 每个 TEST_F 运行后调用
    }

    EventLoop loop; 
};

// 测试用例 1：0个工作线程 (线程池大小为0)
// 预期行为：getNextLoop() 始终返回主循环(loop)
TEST_F(EventLoopThreadPoolTest, SingleThreadZeroPool)
{
    EventLoopThreadPool model(&loop, "single");
    model.setThreadNum(0);
    model.start(init);
    
    // 使用 EXPECT_EQ 替代 assert(a == b)
    EXPECT_EQ(model.getNextLoop(), &loop);
    EXPECT_EQ(model.getNextLoop(), &loop);
    EXPECT_EQ(model.getNextLoop(), &loop);
}

// 测试用例 2：1个工作线程
// 预期行为：getNextLoop() 始终返回那个唯一的工作线程循环，且不等于主循环
TEST_F(EventLoopThreadPoolTest, SingleBackgroundThread)
{
    EventLoopThreadPool model(&loop, "another");
    model.setThreadNum(1);
    model.start(init);
    
    EventLoop* nextLoop = model.getNextLoop();
    // nextLoop->runAfter(2, [nextLoop] { print(nextLoop); });
    nextLoop->runInLoop([nextLoop] { print(nextLoop); });

    
    // 使用 EXPECT_NE 替代 assert(a != b)
    EXPECT_NE(nextLoop, &loop);
    EXPECT_EQ(nextLoop, model.getNextLoop());
    EXPECT_EQ(nextLoop, model.getNextLoop());
    
    // 等待后台线程执行 runAfter 里的 print
    // 注：在真实的单元测试中，我们尽量避免 sleep() 而是使用泥多的 CountDownLatch 来同步
    // 这里为了保留原意，暂留 sleep。
    // ::sleep(3); 
}

// 测试用例 3：3个工作线程
// 预期行为：getNextLoop() 按照轮询(Round-Robin)方式分配线程
TEST_F(EventLoopThreadPoolTest, ThreeThreadsRoundRobin)
{
    EventLoopThreadPool model(&loop, "three");
    model.setThreadNum(3);
    model.start(init);
    
    EventLoop* nextLoop = model.getNextLoop();
    nextLoop->runInLoop([nextLoop] { print(nextLoop); });
    
    EXPECT_NE(nextLoop, &loop);
    // 前面取走了一个，接下来两次取的应该都不等于第一个取出的 loop
    EXPECT_NE(nextLoop, model.getNextLoop());
    EXPECT_NE(nextLoop, model.getNextLoop());
    // 轮询 3 次后，第 4 次应该重新回到第一个 loop
    EXPECT_EQ(nextLoop, model.getNextLoop());
}

// // GTest 的入口
// int main(int argc, char **argv)
// {
//     ::testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }