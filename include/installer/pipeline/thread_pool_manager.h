#pragma once

#include "installer/pipeline/installer_task_manager.h"

#include <thread>
#include <future>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace MultiThreadedInstaller {

/// 解析线程池实际工作线程数：0/过大时回退到合理的 CPU 相关默认值。
size_t ResolveThreadPoolWorkerCount(size_t requestedThreadCount);

/// 通用线程池（对 InstallerTaskManager 的轻量封装）：提交可调用对象并返回 future。
/// 不可拷贝；析构会停止并回收线程。
class ThreadPoolManager {
public:
    /// @param threadCount 工作线程数，默认取硬件并发度。
    explicit ThreadPoolManager(size_t threadCount = std::thread::hardware_concurrency());
    ~ThreadPoolManager();

    ThreadPoolManager(const ThreadPoolManager&) = delete;
    ThreadPoolManager& operator=(const ThreadPoolManager&) = delete;

    /// 提交一个任务，返回其结果的 future。
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    /// 阻塞直到所有已提交任务完成。
    void waitForAll();

    size_t getActiveThreadCount() const;  ///< 当前正在执行任务的线程数。
    size_t getTotalThreadCount() const;   ///< 线程池总线程数。

    /// 停止线程池，唤醒并回收所有工作线程（之后不再接受新任务）。
    void stop();

private:
    std::unique_ptr<InstallerTaskManager> taskManager_;  ///< 底层任务管理器。
};


template<typename F, typename... Args>
auto ThreadPoolManager::enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    return taskManager_->enqueue(std::forward<F>(f), std::forward<Args>(args)...);
}

} // namespace MultiThreadedInstaller
