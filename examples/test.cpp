#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>
#include <future>
#include <iomanip>
#include <numeric>

using namespace std::chrono;

// =============================================================
// 1. 单核浮点运算测试：密集的双精度泰勒展开/积分求 Pi
// =============================================================
double benchmark_fp64(int iterations) {
    double pi = 0.0;
    double sign = 1.0;
    // 使用莱布尼茨公式和微积分积分累加，防止被简单向量化
    for (int i = 0; i < iterations; ++i) {
        double x = (i + 0.5) / iterations;
        pi += 4.0 / (1.0 + x * x);
    }
    return pi / iterations;
}

// =============================================================
// 2. 单核整数与分支预测测试：埃拉托斯特尼筛法求素数
// =============================================================
uint64_t benchmark_integer(int limit) {
    std::vector<bool> is_prime(limit + 1, true);
    is_prime[0] = is_prime[1] = false;

    for (int p = 2; p * p <= limit; p++) {
        if (is_prime[p]) {
            for (int i = p * p; i <= limit; i += p)
                is_prime[i] = false;
        }
    }

    uint64_t count = 0;
    for (int p = 2; p <= limit; p++) {
        if (is_prime[p]) count++;
    }
    return count;
}

// =============================================================
// 3. 多核运算工作线程（浮点吞吐）
// =============================================================
double worker_task(int iterations) {
    double sum = 0.0;
    for (int i = 0; i < iterations; ++i) {
        sum += std::sin(i * 0.0001) * std::cos(i * 0.0001) + std::sqrt(i + 1.0);
    }
    return sum;
}

// =============================================================
// 主程序：运行测试并评分
// =============================================================
int main() {
    std::cout << "==============================================\n";
    std::cout << "        C++ CPU Baseline Benchmark            \n";
    std::cout << "==============================================\n\n";

    unsigned int cores = std::thread::hardware_concurrency();
    std::cout << "[*] 检测到可用 CPU 逻辑核心数: " << (cores > 0 ? cores : 4) << "\n\n";

    // ---------------------------------------------------------
    // 测试一：单核浮点性能 (Single-Core FP64)
    // ---------------------------------------------------------
    const int FP_ITERATIONS = 500'000'000;
    std::cout << "[1/3] 正在测试单核浮点运算 (FP64)... " << std::flush;
    auto t1 = high_resolution_clock::now();
    volatile double fp_res = benchmark_fp64(FP_ITERATIONS); // volatile 防止优化消除
    auto t2 = high_resolution_clock::now();
    double fp_time = duration<double, std::milli>(t2 - t1).count();
    std::cout << "完成!\n";
    std::cout << "      -> 耗时: " << std::fixed << std::setprecision(2) << fp_time << " ms\n\n";

    // ---------------------------------------------------------
    // 测试二：单核整型与分支性能 (Single-Core Integer)
    // ---------------------------------------------------------
    const int INT_LIMIT = 200'000'000;
    std::cout << "[2/3] 正在测试单核整型与分支预测... " << std::flush;
    t1 = high_resolution_clock::now();
    volatile uint64_t prime_count = benchmark_integer(INT_LIMIT);
    t2 = high_resolution_clock::now();
    double int_time = duration<double, std::milli>(t2 - t1).count();
    std::cout << "完成!\n";
    std::cout << "      -> 耗时: " << std::fixed << std::setprecision(2) << int_time << " ms\n";
    std::cout << "      -> 素数计数效验: " << prime_count << "\n\n";

    // ---------------------------------------------------------
    // 测试三：多核多线程并发吞吐 (Multi-Core FP)
    // ---------------------------------------------------------
    const int MULTI_ITERATIONS = 100'000'000;
    std::cout << "[3/3] 正在测试多核全开吞吐 (" << cores << " 核心负载)... " << std::flush;
    
    // 单线程基准参考耗时
    t1 = high_resolution_clock::now();
    volatile double ref = worker_task(MULTI_ITERATIONS);
    t2 = high_resolution_clock::now();
    double single_thread_ref_time = duration<double, std::milli>(t2 - t1).count();

    // 多线程并发测试
    t1 = high_resolution_clock::now();
    std::vector<std::future<double>> futures;
    for (unsigned int i = 0; i < cores; ++i) {
        futures.push_back(std::async(std::launch::async, worker_task, MULTI_ITERATIONS));
    }
    double total_sum = 0.0;
    for (auto& f : futures) {
        total_sum += f.get();
    }
    t2 = high_resolution_clock::now();
    double multi_time = duration<double, std::milli>(t2 - t1).count();
    
    // 防止编译消除
    volatile double check = total_sum;
    (void)check; (void)fp_res;

    std::cout << "完成!\n";
    std::cout << "      -> 单线程基础耗时: " << std::fixed << std::setprecision(2) << single_thread_ref_time << " ms\n";
    std::cout << "      -> 多核满载总耗时: " << std::fixed << std::setprecision(2) << multi_time << " ms\n\n";

    // ---------------------------------------------------------
    // 计算终评得分
    // ---------------------------------------------------------
    // 得分是以参考基准推导的相对数值，数值越低耗时越短，得分越高
    double single_score = (1000.0 / fp_time * 5000) + (1000.0 / int_time * 5000);
    double multi_ratio = (single_thread_ref_time * cores) / multi_time;
    double multi_score = single_score * (cores * (1000.0 / multi_time) / (1000.0 / single_thread_ref_time)) * 0.15;

    std::cout << "==============================================\n";
    std::cout << "              测试结果与终评                  \n";
    std::cout << "==============================================\n";
    std::cout << "  单核综合得分 (Single-Core Score) : " << (int)single_score << "\n";
    std::cout << "  多核综合得分 (Multi-Core Score)  : " << (int)multi_score << "\n";
    std::cout << "  多核并发效率 (Scaling Ratio)     : " << std::setprecision(2) << multi_ratio << "x\n";
    std::cout << "==============================================\n";

    return 0;
}