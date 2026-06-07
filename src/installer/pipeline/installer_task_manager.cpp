#include "installer/pipeline/installer_task_manager.h"

#include <algorithm>
#include <utility>

namespace MultiThreadedInstaller {

InstallerTaskManager::InstallerTaskManager(size_t workerCount, std::string name)
    : name_(std::move(name)) {
    const size_t resolvedWorkerCount = (std::max)(size_t{1}, workerCount);
    workers_.reserve(resolvedWorkerCount);
    for (size_t i = 0; i < resolvedWorkerCount; ++i) {
        workers_.emplace_back(&InstallerTaskManager::workerLoop, this);
    }
}

InstallerTaskManager::~InstallerTaskManager() {
    stop();
}

void InstallerTaskManager::submit(std::function<void()> task) {
    enqueueTask([task = std::move(task), name = name_]() mutable {
        try {
            task();
        } catch (const std::exception& e) {
            logInstallerError("[" + name + "] Task execution failed: " + e.what());
        } catch (...) {
            logInstallerError("[" + name + "] Task execution failed: unknown exception");
        }
    });
}

void InstallerTaskManager::waitForAll() {
    std::unique_lock<std::mutex> lock(mutex_);
    allTasksComplete_.wait(lock, [this]() {
        return pendingTasks_ == 0 && activeTasks_ == 0 && tasks_.empty();
    });
}

void InstallerTaskManager::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ && workers_.empty()) {
            return;
        }
        stopping_ = true;
    }

    hasTask_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

size_t InstallerTaskManager::activeWorkerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeTasks_;
}

size_t InstallerTaskManager::totalWorkerCount() const {
    return workers_.size();
}

InstallerTaskStats InstallerTaskManager::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return InstallerTaskStats{pendingTasks_, activeTasks_, completedTasks_};
}

void InstallerTaskManager::enqueueTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("enqueue on stopped InstallerTaskManager");
        }
        tasks_.push(std::move(task));
        ++pendingTasks_;
    }
    hasTask_.notify_one();
}

void InstallerTaskManager::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            hasTask_.wait(lock, [this]() {
                return stopping_ || !tasks_.empty();
            });

            if (stopping_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
            ++activeTasks_;
        }

        task();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingTasks_ > 0) {
                --pendingTasks_;
            }
            if (activeTasks_ > 0) {
                --activeTasks_;
            }
            ++completedTasks_;
        }
        allTasksComplete_.notify_all();
    }
}

} // namespace MultiThreadedInstaller
