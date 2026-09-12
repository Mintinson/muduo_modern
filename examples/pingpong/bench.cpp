///
/// @file examples/pingpong/bench.cpp
/// @brief 专业 pingpong benchmark — 三种模式：吞吐量 / 延迟 / 连接速率
///
/// 用法:
///   ./pingpong_bench -m throughput [-t threads] [-s sessions] [-b blockSize] [-d duration]
///   ./pingpong_bench -m latency     [-n pipes]    [-a active]    [-w writes]
///   ./pingpong_bench -m connrate    [-t threads] [-s sessions]
///

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/net/Channel.hpp"
#include "chaoxi/net/EventLoop.hpp"
#include "chaoxi/net/EventLoopThreadPool.hpp"
#include "chaoxi/net/InetAddress.hpp"
#include "chaoxi/net/TcpClient.hpp"
#include "chaoxi/net/TcpServer.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <thread>
#include <vector>

#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace chaoxi;
using namespace chaoxi::net;

// ============================================================================
// helpers
// ============================================================================

void setLoggerWarn() { Logger::setLogLevel(Logger::LogLevel::WARN); }

double nowSec() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

// ============================================================================
// mode: throughput
// ============================================================================

struct ThroughputStats {
    std::atomic<int64_t> bytesRead{0};
    std::atomic<int64_t> messagesRead{0};
};

void runThroughput(int threads, int sessions, int blockSize, int durationSec) {
    setLoggerWarn();
    ThroughputStats stats;

    // ---- server ----
    std::jthread serverThread([threads]() {
        EventLoop srvLoop;
        InetAddress listenAddr(9988);
        TcpServer server(&srvLoop, listenAddr, "bench_server");
        server.setConnectionCallback([](const TcpConnectionPtr& conn) {
            if (conn->connected()) conn->setTcpNoDelay(true);
        });
        server.setMessageCallback([](const TcpConnectionPtr& conn,
                                     Buffer& buf, Timestamp) {
            conn->send(std::move(buf));
        });
        server.setThreadNum(threads);
        server.start();
        srvLoop.loop();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // ---- clients (dedicated thread with own EventLoop) ----
    std::string payload(static_cast<size_t>(blockSize), 'x');
    for (int i = 0; i < blockSize; ++i)
        payload[i] = static_cast<char>(i % 128);

    std::atomic<double> t0Atomic{0};
    std::atomic<int32_t> connected{0};

    std::jthread clientThread([&]() {
        EventLoop cliLoop;
        InetAddress serverAddr("127.0.0.1", 9988);
        std::vector<std::unique_ptr<TcpClient>> clients;
        clients.reserve(sessions);

        for (int i = 0; i < sessions; ++i) {
            auto client = std::make_unique<TcpClient>(&cliLoop, serverAddr,
                                                      "c" + std::to_string(i));
            client->setConnectionCallback([&](const TcpConnectionPtr& conn) {
                if (conn->connected()) {
                    conn->setTcpNoDelay(true);
                    conn->send(payload);
                    int n = ++connected;
                    if (n == 1) {
                        t0Atomic.store(nowSec(), std::memory_order_release);
                    }
                    if (n == sessions) {
                        std::println("all {} connected in {:.3f}s",
                                     n, nowSec() - t0Atomic.load());
                    }
                }
            });
            client->setMessageCallback([&](const TcpConnectionPtr& conn,
                                           Buffer& buf, Timestamp) {
                stats.bytesRead += buf.readableBytes();
                stats.messagesRead++;
                conn->send(std::move(buf));
            });
            client->connect();
            clients.push_back(std::move(client));
        }

        cliLoop.runAfter(durationSec, [&]() { cliLoop.quit(); });
        cliLoop.loop();

        for (auto& c : clients) c->stop();
        clients.clear();
    });

    clientThread.join();
    double t1 = nowSec();
    double t0 = t0Atomic.load();

    double elapsed = t1 - t0;
    if (elapsed <= 0) elapsed = durationSec;  // fallback
    double mb = stats.bytesRead.load() / (1024.0 * 1024.0);
    double mbps = mb / elapsed;

    std::println("---throughput_result---");
    std::println("mode=throughput threads={} sessions={} blockSize={} duration={}s",
                 threads, sessions, blockSize, durationSec);
    std::println("bytes_read={} messages_read={} elapsed={:.3f}s throughput_mbps={:.2f} avg_msg_size={:.1f}",
                 stats.bytesRead.load(), stats.messagesRead.load(),
                 elapsed, mbps,
                 stats.messagesRead > 0
                     ? static_cast<double>(stats.bytesRead) / stats.messagesRead
                     : 0.0);

    fflush(stdout);
    _exit(0);  // skip clean destructors, just exit
}

// ============================================================================
// mode: latency (socketpair pingpong)
// ============================================================================

static EventLoop* g_loop = nullptr;
static std::vector<std::unique_ptr<Channel>> g_channels;
static std::vector<int> g_fds;
static int g_reads = 0, g_writes = 0, g_totalWrites = 0;
static int g_numActive = 1, g_numWrites = 100;
static std::vector<int> g_writeCounts;
static int g_activeIdx = 0;

void latencyReadCallback(Timestamp) {
    int idx = g_activeIdx;
    g_activeIdx = (g_activeIdx + 1) % g_numActive;
    char ch = 0;
    ::recv(g_channels[idx]->fd(), &ch, 1, 0);
    g_reads++;

    int widx = (idx + 1) % g_numActive;
    int& wc = g_writeCounts[widx];
    if (wc < g_numWrites) {
        ::send(g_fds[widx], "m", 1, 0);
        wc++;
        g_writes++;
    }
    if (g_reads >= g_totalWrites) {
        g_loop->quit();
    }
}

void runLatency(int numPipes, int numActive, int numWrites) {
    setLoggerWarn();
    g_numActive = numActive;
    g_numWrites = numWrites;
    g_totalWrites = numActive * numWrites;

    struct rlimit rl{};
    rl.rlim_cur = rl.rlim_max = static_cast<rlim_t>(numPipes * 2 + 50);
    ::setrlimit(RLIMIT_NOFILE, &rl);

    EventLoop loop;
    g_loop = &loop;

    g_channels.clear();
    g_fds.clear();
    g_channels.reserve(numActive);
    g_fds.reserve(numPipes);

    std::vector<int> allPairs(numPipes * 2);
    for (int i = 0; i < numPipes; ++i) {
        int p[2];
        ::socketpair(AF_UNIX, SOCK_STREAM, 0, p);
        allPairs[i * 2] = p[0];
        allPairs[i * 2 + 1] = p[1];
        g_fds.push_back(p[1]);
    }

    for (int i = 0; i < numActive; ++i) {
        auto ch = std::make_unique<Channel>(&loop, allPairs[i * 2]);
        ch->setReadCallback(latencyReadCallback);
        ch->enableReading();
        g_channels.push_back(std::move(ch));
    }

    std::println("---latency_result---");
    std::println("mode=latency pipes={} active={} writes={}",
                 numPipes, numActive, numWrites);
    std::println("iteration,elapsed_us,loop_us,us_per_msg");

    for (int iter = 0; iter < 25; ++iter) {
        g_reads = g_writes = 0;
        g_activeIdx = 0;
        g_writeCounts.assign(numActive, 0);

        auto t0 = std::chrono::system_clock::now();
        for (int i = 0; i < numActive; ++i) {
            ::send(g_fds[i], "m", 1, 0);
            g_writes++;
            g_writeCounts[i]++;
        }
        auto t1 = std::chrono::system_clock::now();
        loop.loop();
        auto t2 = std::chrono::system_clock::now();

        auto injectUs = std::chrono::duration_cast<std::chrono::microseconds>(
                            t1 - t0).count();
        auto loopUs = std::chrono::duration_cast<std::chrono::microseconds>(
                          t2 - t1).count();
        auto totalUs = injectUs + loopUs;
        double perMsg = static_cast<double>(totalUs) / g_totalWrites;

        std::println("{},{},{},{:.3f}", iter, totalUs, loopUs, perMsg);
    }

    g_channels.clear();
    for (int i = 0; i < numPipes; ++i) {
        ::close(allPairs[i * 2]);
        ::close(g_fds[i]);
    }
    g_fds.clear();
}

// ============================================================================
// mode: connrate
// ============================================================================

void runConnRate(int threads, int sessions) {
    setLoggerWarn();
    std::atomic<int64_t> accepted{0};
    std::promise<void> listening;
    auto ready = listening.get_future();
    std::jthread serverThread([&] {
        EventLoop serverLoop;
        TcpServer server(&serverLoop, InetAddress(9989), "cr_server");
        server.setConnectionCallback([&](const TcpConnectionPtr& conn) {
            if (conn->connected()) accepted++;
        });
        server.setThreadNum(threads);
        server.start();
        listening.set_value();
        serverLoop.loop();
    });
    ready.wait();

    EventLoop clientLoop;
    InetAddress serverAddr("127.0.0.1", 9989);
    std::vector<std::unique_ptr<TcpClient>> clients;
    clients.reserve(sessions);
    std::atomic<int64_t> connected{0};
    const auto t0 = nowSec();

    for (int i = 0; i < sessions; ++i) {
        auto client = std::make_unique<TcpClient>(&clientLoop, serverAddr,
                                                  "cr" + std::to_string(i));
        client->setConnectionCallback([&](const TcpConnectionPtr& conn) {
            if (conn->connected()) {
                if (++connected == sessions) clientLoop.quit();
                conn->shutdown();
            }
        });
        client->connect();
        clients.push_back(std::move(client));
    }
    clientLoop.runAfter(10.0, [&] { clientLoop.quit(); });
    clientLoop.loop();

    const double elapsed = nowSec() - t0;
    const double rate = connected / elapsed;
    std::println("---connrate_result---");
    std::println("mode=connrate threads={} sessions={}", threads, sessions);
    std::println("connected={} accepted={} elapsed={:.3f}s conn_per_sec={:.1f}",
                 connected.load(), accepted.load(), elapsed, rate);

    fflush(stdout);
    _exit(0);  // skip clean destructors to avoid cross-thread cleanup issues
}

// ============================================================================
// main
// ============================================================================

static void usage(const char* prog) {
    std::println("Usage:");
    std::println("  {} -m throughput [-t threads] [-s sessions] [-b blockSize] [-d duration]", prog);
    std::println("  {} -m latency     [-n pipes] [-a active] [-w writes]", prog);
    std::println("  {} -m connrate    [-t threads] [-s sessions]", prog);
    std::println("Defaults:");
    std::println("  throughput: -t 2 -s 100 -b 1024 -d 10");
    std::println("  latency:    -n 100 -a 1 -w 100");
    std::println("  connrate:   -t 2 -s 1000");
    exit(1);
}

int main(int argc, char* argv[]) {
    std::string mode;
    int threads = 2, sessions = 100, blockSize = 1024, duration = 10;
    int pipes = 100, active = 1, writes = 100;

    int opt;
    while ((opt = getopt(argc, argv, "m:t:s:b:d:n:a:w:h")) != -1) {
        switch (opt) {
            case 'm': mode = optarg; break;
            case 't': threads = atoi(optarg); break;
            case 's': sessions = atoi(optarg); break;
            case 'b': blockSize = atoi(optarg); break;
            case 'd': duration = atoi(optarg); break;
            case 'n': pipes = atoi(optarg); break;
            case 'a': active = atoi(optarg); break;
            case 'w': writes = atoi(optarg); break;
            case 'h': default: usage(argv[0]);
        }
    }

    if (mode.empty()) usage(argv[0]);

    if (mode == "throughput") {
        runThroughput(threads, sessions, blockSize, duration);
    } else if (mode == "latency") {
        runLatency(pipes, active, writes);
    } else if (mode == "connrate") {
        runConnRate(threads, sessions);
    } else {
        std::println("Unknown mode: {}", mode);
        usage(argv[0]);
    }
    return 0;
}
