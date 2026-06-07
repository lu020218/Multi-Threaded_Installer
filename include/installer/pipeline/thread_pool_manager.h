#pragma once

#include "installer/pipeline/installer_task_manager.h"

#include <thread>
#include <future>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace MultiThreadedInstaller {

size_t ResolveThreadPoolWorkerCount(size_t requestedThreadCount);

class ThreadPoolManager {
public:
    explicit ThreadPoolManager(size_t threadCount = std::thread::hardware_concurrency());
    ~ThreadPoolManager();
    

    ThreadPoolManager(const ThreadPoolManager&) = delete;
    ThreadPoolManager& operator=(const ThreadPoolManager&) = delete;
    

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;
    

    void waitForAll();
    

    size_t getActiveThreadCount() const;
    

    size_t getTotalThreadCount() const;
    

    void stop();
    
private:
    std::unique_ptr<InstallerTaskManager> taskManager_;
};


template<typename F, typename... Args>
auto ThreadPoolManager::enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    return taskManager_->enqueue(std::forward<F>(f), std::forward<Args>(args)...);
}

} // namespace MultiThreadedInstaller
