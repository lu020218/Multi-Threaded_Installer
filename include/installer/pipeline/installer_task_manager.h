#pragma once

#include "common/installer_logger.h"

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace MultiThreadedInstaller {

/// 任务管理器的运行统计快照。
struct InstallerTaskStats {
    size_t pending = 0;    ///< 队列中待执行的任务数。
    size_t active = 0;     ///< 正在执行的任务数。
    size_t completed = 0;  ///< 已完成的任务数。
};

/// 固定线程数的任务队列（线程池实现）：worker 循环从队列取任务执行；任务内异常被捕获、
/// 记日志并转存到对应 future。不可拷贝；析构会 stop 并 join 所有 worker。
class InstallerTaskManager {
public:
    /// @param workerCount 工作线程数。@param name 名字（日志前缀用）。
    explicit InstallerTaskManager(size_t workerCount, std::string name = "InstallerTaskManager");
    ~InstallerTaskManager();

    InstallerTaskManager(const InstallerTaskManager&) = delete;
    InstallerTaskManager& operator=(const InstallerTaskManager&) = delete;

    /// 提交带返回值的任务，返回其 future（异常经 future 传播）。
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    /// 提交一个无返回值任务。
    void submit(std::function<void()> task);
    /// 阻塞直到队列清空且所有在执行任务完成。
    void waitForAll();
    /// 停止：置停止标志、唤醒并 join 所有 worker（之后不再接受新任务）。
    void stop();

    size_t activeWorkerCount() const;   ///< 正在执行任务的线程数。
    size_t totalWorkerCount() const;    ///< 总线程数。
    InstallerTaskStats stats() const;   ///< 当前统计快照。

private:
    void workerLoop();                              ///< worker 线程主循环。
    void enqueueTask(std::function<void()> task);   ///< 入队并唤醒一个 worker。

    std::string name_;                              ///< 名字（日志前缀）。
    std::vector<std::thread> workers_;              ///< worker 线程。
    std::queue<std::function<void()>> tasks_;       ///< 任务队列。
    mutable std::mutex mutex_;                      ///< 保护队列与计数。
    std::condition_variable hasTask_;               ///< "有任务/需停止"通知。
    std::condition_variable allTasksComplete_;      ///< "全部完成"通知（waitForAll 等待）。
    bool stopping_ = false;                         ///< 是否正在停止。
    size_t pendingTasks_ = 0;                       ///< 待执行任务数。
    size_t activeTasks_ = 0;                        ///< 执行中任务数。
    size_t completedTasks_ = 0;                     ///< 已完成任务数。
};

template<typename F, typename... Args>
auto InstallerTaskManager::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto boundTask = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    auto promise = std::make_shared<std::promise<ReturnType>>();
    std::future<ReturnType> result = promise->get_future();

    enqueueTask([promise, boundTask = std::move(boundTask), name = name_]() mutable {
        try {
            if constexpr (std::is_void<ReturnType>::value) {
                boundTask();
                promise->set_value();
            } else {
                promise->set_value(boundTask());
            }
        } catch (...) {
            try {
                throw;
            } catch (const std::exception& e) {
                logInstallerError("[" + name + "] Task execution failed: " + e.what());
            } catch (...) {
                logInstallerError("[" + name + "] Task execution failed: unknown exception");
            }
            promise->set_exception(std::current_exception());
        }
    });

    return result;
}

} // namespace MultiThreadedInstaller
