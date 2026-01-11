#include "installer/thread_pool_manager.h"
#include <iostream>

namespace MultiThreadedInstaller {

ThreadPoolManager::ThreadPoolManager(size_t threadCount) 
    : stopFlag(false), activeThreads(0) {
    
    for (size_t i = 0; i < threadCount; ++i) {
        workers.emplace_back(&ThreadPoolManager::workerThread, this);
    }
}

ThreadPoolManager::~ThreadPoolManager() {
    stop();
}

void ThreadPoolManager::waitForAll() {
    std::unique_lock<std::mutex> lock(activeThreadsMutex);
    allTasksComplete.wait(lock, [this] { 
        return tasks.empty() && activeThreads == 0; 
    });
}

size_t ThreadPoolManager::getActiveThreadCount() const {
    std::lock_guard<std::mutex> lock(activeThreadsMutex);
    return activeThreads;
}

size_t ThreadPoolManager::getTotalThreadCount() const {
    return workers.size();
}

void ThreadPoolManager::stop() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stopFlag = true;
    }
    
    condition.notify_all();
    
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    workers.clear();
}

void ThreadPoolManager::workerThread() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] { return stopFlag || !tasks.empty(); });
            
            if (stopFlag && tasks.empty()) {
                return;
            }
            
            task = std::move(tasks.front());
            tasks.pop();
        }
        
        // 增加活跃线程计数
        {
            std::lock_guard<std::mutex> lock(activeThreadsMutex);
            ++activeThreads;
        }
        
        // 执行任务
        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "Task execution failed: " << e.what() << std::endl;
        }
        
        // 减少活跃线程计数
        {
            std::lock_guard<std::mutex> lock(activeThreadsMutex);
            --activeThreads;
            if (activeThreads == 0 && tasks.empty()) {
                allTasksComplete.notify_all();
            }
        }
    }
}

} // namespace MultiThreadedInstaller