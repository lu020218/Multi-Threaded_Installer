#include "installer/thread_pool_manager.h"
#include "common/installer_logger.h"

namespace MultiThreadedInstaller {

ThreadPoolManager::ThreadPoolManager(size_t threadCount) 
    : stopFlag(false), pendingTasks(0), activeThreads(0) {
    
    for (size_t i = 0; i < threadCount; ++i) {
        workers.emplace_back(&ThreadPoolManager::workerThread, this);
    }
}

ThreadPoolManager::~ThreadPoolManager() {
    stop();
}

void ThreadPoolManager::waitForAll() {
    std::unique_lock<std::mutex> lock(queueMutex);
    allTasksComplete.wait(lock, [this] { 
        return pendingTasks == 0;
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
        

        {
            std::lock_guard<std::mutex> lock(activeThreadsMutex);
            ++activeThreads;
        }
        

        try {
            task();
        } catch (const std::exception& e) {
            logInstallerError(std::string("[ThreadPool] Task execution failed: ") + e.what());
        } catch (...) {
            logInstallerError("[ThreadPool] Task execution failed: unknown exception");
        }
        

        {
            std::lock_guard<std::mutex> lock(activeThreadsMutex);
            --activeThreads;
        }

        bool notifyComplete = false;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (pendingTasks > 0) {
                --pendingTasks;
            }
            notifyComplete = (pendingTasks == 0);
        }
        if (notifyComplete) {
            allTasksComplete.notify_all();
        }
    }
}

} // namespace MultiThreadedInstaller
