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

struct InstallerTaskStats {
    size_t pending = 0;
    size_t active = 0;
    size_t completed = 0;
};

class InstallerTaskManager {
public:
    explicit InstallerTaskManager(size_t workerCount, std::string name = "InstallerTaskManager");
    ~InstallerTaskManager();

    InstallerTaskManager(const InstallerTaskManager&) = delete;
    InstallerTaskManager& operator=(const InstallerTaskManager&) = delete;

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    void submit(std::function<void()> task);
    void waitForAll();
    void stop();

    size_t activeWorkerCount() const;
    size_t totalWorkerCount() const;
    InstallerTaskStats stats() const;

private:
    void workerLoop();
    void enqueueTask(std::function<void()> task);

    std::string name_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable hasTask_;
    std::condition_variable allTasksComplete_;
    bool stopping_ = false;
    size_t pendingTasks_ = 0;
    size_t activeTasks_ = 0;
    size_t completedTasks_ = 0;
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
