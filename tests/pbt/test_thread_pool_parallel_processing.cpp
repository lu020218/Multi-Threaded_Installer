#include "installer/thread_pool_manager.h"
#include <atomic>
#include <chrono>
#include <vector>
#include <algorithm>
#include <thread>
#include <random>
#include <iostream>
#include <cassert>

using namespace MultiThreadedInstaller;

// Simple property-based testing framework
class PropertyTest {
public:
    static bool runTest(const std::string& name, std::function<bool()> testFunc, int iterations = 100) {
        std::cout << "Running property test: " << name << " (" << iterations << " iterations)... ";
        
        for (int i = 0; i < iterations; ++i) {
            try {
                if (!testFunc()) {
                    std::cout << "FAILED at iteration " << (i + 1) << std::endl;
                    return false;
                }
            } catch (const std::exception& e) {
                std::cout << "FAILED with exception at iteration " << (i + 1) << ": " << e.what() << std::endl;
                return false;
            }
        }
        
        std::cout << "PASSED" << std::endl;
        return true;
    }
};

// Random number generator
std::random_device rd;
std::mt19937 gen(rd());

int randomInt(int min, int max) {
    std::uniform_int_distribution<int> dis(min, max);
    return dis(gen);
}

int main() {
    std::cout << "Running ThreadPoolManager Property-Based Tests" << std::endl;
    
    // **功能：multi-threaded-installer，属性 6：多线程并行处理**
    // **验证：需求 3.1, 3.2, 3.3, 3.4**
    bool success = PropertyTest::runTest("Multi-threaded parallel processing", []() -> bool {
        // 生成测试参数
        int threadCount = randomInt(2, 8);  // 2-8个线程
        int taskCount = randomInt(threadCount * 2, threadCount * 10);  // 任务数是线程数的2-10倍
        int taskDuration = randomInt(10, 100);  // 每个任务10-100毫秒
        
        ThreadPoolManager pool(threadCount);
        
        // 验证线程池创建正确
        if (pool.getTotalThreadCount() != threadCount) {
            return false;
        }
        
        // 用于跟踪并行执行的变量
        std::atomic<int> activeTaskCount{0};
        std::atomic<int> maxConcurrentTasks{0};
        std::atomic<int> completedTasks{0};
        std::vector<std::chrono::steady_clock::time_point> taskStartTimes(taskCount);
        std::vector<std::chrono::steady_clock::time_point> taskEndTimes(taskCount);
        
        // 提交任务
        std::vector<std::future<int>> futures;
        auto submitStartTime = std::chrono::steady_clock::now();
        
        for (int i = 0; i < taskCount; ++i) {
            futures.push_back(pool.enqueue([i, taskDuration, &activeTaskCount, &maxConcurrentTasks, 
                                          &completedTasks, &taskStartTimes, &taskEndTimes]() -> int {
                taskStartTimes[i] = std::chrono::steady_clock::now();
                
                // 增加活跃任务计数
                int currentActive = activeTaskCount.fetch_add(1) + 1;
                
                // 更新最大并发任务数
                int expectedMax = maxConcurrentTasks.load();
                while (expectedMax < currentActive && 
                       !maxConcurrentTasks.compare_exchange_weak(expectedMax, currentActive)) {
                    expectedMax = maxConcurrentTasks.load();
                }
                
                // 模拟工作
                std::this_thread::sleep_for(std::chrono::milliseconds(taskDuration));
                
                // 减少活跃任务计数
                activeTaskCount.fetch_sub(1);
                completedTasks.fetch_add(1);
                
                taskEndTimes[i] = std::chrono::steady_clock::now();
                return i;
            }));
        }
        
        // 等待所有任务完成
        std::vector<int> results;
        for (auto& future : futures) {
            results.push_back(future.get());
        }
        
        auto totalEndTime = std::chrono::steady_clock::now();
        
        // 验证所有任务都完成了
        if (completedTasks.load() != taskCount) {
            return false;
        }
        if (results.size() != taskCount) {
            return false;
        }
        
        // 验证结果正确性（每个任务返回其索引）
        std::sort(results.begin(), results.end());
        for (int i = 0; i < taskCount; ++i) {
            if (results[i] != i) {
                return false;
            }
        }
        
        // 验证并行处理特性
        
        // 1. 应该有多个任务并发执行（最大并发数应该 > 1）
        if (maxConcurrentTasks.load() <= 1) {
            return false;
        }
        
        // 2. 最大并发数不应该超过线程池大小
        if (maxConcurrentTasks.load() > threadCount) {
            return false;
        }
        
        // 3. 如果任务数多于线程数，应该有明显的并行效果
        if (taskCount > threadCount) {
            // 计算总的工作时间（所有任务的持续时间之和）
            auto totalWorkTime = taskCount * taskDuration;
            
            // 计算实际执行时间
            auto actualExecutionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                totalEndTime - submitStartTime).count();
            
            // 并行执行应该比串行执行快
            // 允许一些开销，所以实际时间应该小于串行时间的80%
            if (actualExecutionTime >= totalWorkTime * 0.8) {
                return false;
            }
        }
        
        // 4. 验证任务重叠执行（至少有一些任务在时间上重叠）
        int overlappingPairs = 0;
        for (int i = 0; i < taskCount; ++i) {
            for (int j = i + 1; j < taskCount; ++j) {
                // 检查任务i和任务j是否在时间上重叠
                if (taskStartTimes[i] < taskEndTimes[j] && taskStartTimes[j] < taskEndTimes[i]) {
                    overlappingPairs++;
                }
            }
        }
        
        // 如果有足够的任务和线程，应该有一些重叠
        if (taskCount >= threadCount && threadCount > 1) {
            if (overlappingPairs <= 0) {
                return false;
            }
        }
        
        // 5. 验证waitForAll功能
        std::atomic<int> additionalTasksCompleted{0};
        
        // 提交额外的任务
        for (int i = 0; i < 3; ++i) {
            pool.enqueue([&additionalTasksCompleted]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                additionalTasksCompleted.fetch_add(1);
            });
        }
        
        // 等待所有任务完成
        pool.waitForAll();
        
        // 验证额外任务也完成了
        if (additionalTasksCompleted.load() != 3) {
            return false;
        }
        
        // 6. 验证线程池在高负载下的稳定性
        if (pool.getActiveThreadCount() != 0) { // 所有任务完成后应该没有活跃线程
            return false;
        }
        
        return true;
    }, 50); // Run 50 iterations
    
    if (success) {
        std::cout << "All ThreadPoolManager property-based tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "ThreadPoolManager property-based tests failed!" << std::endl;
        return 1;
    }
}