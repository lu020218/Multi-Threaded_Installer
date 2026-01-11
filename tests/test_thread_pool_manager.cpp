#include "installer/thread_pool_manager.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <atomic>
#include <vector>

using namespace MultiThreadedInstaller;

// Simple test framework
void runTest(const std::string& testName, bool (*testFunc)()) {
    std::cout << "Running " << testName << "... ";
    if (testFunc()) {
        std::cout << "PASSED" << std::endl;
    } else {
        std::cout << "FAILED" << std::endl;
        exit(1);
    }
}

// Test basic thread pool creation and destruction
bool testThreadPoolCreation() {
    ThreadPoolManager pool(4);
    return pool.getTotalThreadCount() == 4;
}

// Test task enqueueing and execution
bool testTaskExecution() {
    ThreadPoolManager pool(2);
    std::atomic<int> counter{0};
    
    // Enqueue multiple tasks
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.enqueue([&counter]() {
            counter.fetch_add(1);
        }));
    }
    
    // Wait for all tasks to complete
    for (auto& future : futures) {
        future.wait();
    }
    
    return counter.load() == 10;
}

// Test task with return value
bool testTaskWithReturnValue() {
    ThreadPoolManager pool(2);
    
    auto future = pool.enqueue([]() -> int {
        return 42;
    });
    
    return future.get() == 42;
}

// Test task with parameters
bool testTaskWithParameters() {
    ThreadPoolManager pool(2);
    
    auto future = pool.enqueue([](int a, int b) -> int {
        return a + b;
    }, 10, 20);
    
    return future.get() == 30;
}

// Test waitForAll functionality
bool testWaitForAll() {
    ThreadPoolManager pool(2);
    std::atomic<int> counter{0};
    
    // Enqueue tasks that take some time
    for (int i = 0; i < 5; ++i) {
        pool.enqueue([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter.fetch_add(1);
        });
    }
    
    // Wait for all tasks to complete
    pool.waitForAll();
    
    return counter.load() == 5;
}

// Test exception handling in tasks
bool testExceptionHandling() {
    ThreadPoolManager pool(2);
    std::atomic<bool> taskExecuted{false};
    
    // Enqueue a task that throws an exception
    auto future1 = pool.enqueue([]() {
        throw std::runtime_error("Test exception");
    });
    
    // Enqueue a normal task after the exception
    auto future2 = pool.enqueue([&taskExecuted]() {
        taskExecuted.store(true);
    });
    
    // The exception should be contained in the future
    try {
        future1.get();
        return false; // Should have thrown
    } catch (const std::runtime_error&) {
        // Expected
    }
    
    // The second task should still execute
    future2.wait();
    return taskExecuted.load();
}

// Test concurrent task execution
bool testConcurrentExecution() {
    ThreadPoolManager pool(4);
    std::atomic<int> activeCount{0};
    std::atomic<int> maxConcurrent{0};
    
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < 8; ++i) {
        futures.push_back(pool.enqueue([&activeCount, &maxConcurrent]() {
            int current = activeCount.fetch_add(1) + 1;
            
            // Update max concurrent if needed
            int expected = maxConcurrent.load();
            while (expected < current && !maxConcurrent.compare_exchange_weak(expected, current)) {
                expected = maxConcurrent.load();
            }
            
            // Simulate work
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            activeCount.fetch_sub(1);
        }));
    }
    
    // Wait for all tasks
    for (auto& future : futures) {
        future.wait();
    }
    
    // Should have had at least 2 concurrent tasks (we have 4 threads and 8 tasks)
    return maxConcurrent.load() >= 2;
}

// Test stop functionality
bool testStopFunctionality() {
    ThreadPoolManager pool(2);
    std::atomic<int> counter{0};
    
    // Enqueue some quick tasks
    for (int i = 0; i < 3; ++i) {
        pool.enqueue([&counter]() {
            counter.fetch_add(1);
        });
    }
    
    // Wait a bit for tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Stop the pool
    pool.stop();
    
    // Try to enqueue after stop (should throw)
    try {
        pool.enqueue([]() {});
        return false; // Should have thrown
    } catch (const std::runtime_error&) {
        return true; // Expected
    }
}

int main() {
    std::cout << "Running ThreadPoolManager Unit Tests" << std::endl;
    
    runTest("Thread Pool Creation", testThreadPoolCreation);
    runTest("Task Execution", testTaskExecution);
    runTest("Task With Return Value", testTaskWithReturnValue);
    runTest("Task With Parameters", testTaskWithParameters);
    runTest("Wait For All", testWaitForAll);
    runTest("Exception Handling", testExceptionHandling);
    runTest("Concurrent Execution", testConcurrentExecution);
    runTest("Stop Functionality", testStopFunctionality);
    
    std::cout << "All ThreadPoolManager tests passed!" << std::endl;
    return 0;
}