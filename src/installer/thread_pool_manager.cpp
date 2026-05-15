#include "installer/thread_pool_manager.h"

namespace MultiThreadedInstaller {

size_t ResolveThreadPoolWorkerCount(size_t requestedThreadCount) {
    if (requestedThreadCount > 0) {
        return requestedThreadCount;
    }
    const size_t fallback = static_cast<size_t>(std::thread::hardware_concurrency());
    return fallback > 0 ? fallback : 1;
}

ThreadPoolManager::ThreadPoolManager(size_t threadCount) 
    : taskManager_(std::make_unique<InstallerTaskManager>(
          ResolveThreadPoolWorkerCount(threadCount),
          "ThreadPoolManager")) {}

ThreadPoolManager::~ThreadPoolManager() {
    stop();
}

void ThreadPoolManager::waitForAll() {
    taskManager_->waitForAll();
}

size_t ThreadPoolManager::getActiveThreadCount() const {
    return taskManager_->activeWorkerCount();
}

size_t ThreadPoolManager::getTotalThreadCount() const {
    return taskManager_->totalWorkerCount();
}

void ThreadPoolManager::stop() {
    if (taskManager_) {
        taskManager_->stop();
    }
}

} // namespace MultiThreadedInstaller
