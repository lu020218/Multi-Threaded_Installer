#include "installer/cleanup_delete_executor.h"

#include "common/installer_logger.h"
#include "installer/installer_helpers.h"
#include "installer/installer_task_manager.h"

#include <chrono>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

namespace {

bool IsCleanupReparsePoint(const std::filesystem::path& path) {
#ifdef _WIN32
    const DWORD attrs = GetFileAttributesW(toLongPath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == FILE_ATTRIBUTE_REPARSE_POINT;
#else
    std::error_code ec;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec));
#endif
}

std::string CurrentThreadIdString() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

} // namespace

uint64_t CleanupDeleteNowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

class CleanupDeleteExecutor::Impl {
public:
    Impl(CleanupDeleteWorkload workload,
         CleanupDeleteCallbacks callbacks)
        : workload_(workload),
          concurrency_(ResolveCleanupDeleteConcurrency(workload)),
          callbacks_(std::move(callbacks)) {}

    ~Impl() {
        finish();
    }

    void start() {
        std::lock_guard<std::mutex> lock(startMutex_);
        if (started_) {
            return;
        }
        started_ = true;
        logInstallerInfo("[Cleanup][Concurrency] workload=" + CleanupDeleteWorkloadName(workload_) +
                         " hwThreads=" + std::to_string(GetInstallerHardwareConcurrency()) +
                         " cleanupWorkers=" + std::to_string(concurrency_));
        taskManager_ = std::make_unique<InstallerTaskManager>(
            concurrency_,
            "CleanupDeleteExecutor:" + CleanupDeleteWorkloadName(workload_));
    }

    void submit(std::vector<std::filesystem::path>& files) {
        if (files.empty()) {
            return;
        }
        start();
        for (auto& file : files) {
            taskManager_->submit([this, path = std::move(file)]() mutable {
                deleteOne(std::move(path));
            });
        }
        files.clear();
    }

    void finish() {
        std::unique_ptr<InstallerTaskManager> manager;
        {
            std::lock_guard<std::mutex> lock(startMutex_);
            if (!started_) {
                return;
            }
            manager = std::move(taskManager_);
            started_ = false;
        }
        if (manager) {
            manager->waitForAll();
            manager->stop();
        }
    }

    uint32_t workerConcurrency() const {
        return concurrency_;
    }

private:
    void deleteOne(std::filesystem::path path) {
        if (path.empty()) {
            if (callbacks_.onEmptyPathSkipped) {
                callbacks_.onEmptyPathSkipped();
            }
            return;
        }
        if (IsCleanupReparsePoint(path)) {
            if (callbacks_.onReparsePointSkipped) {
                callbacks_.onReparsePointSkipped(path);
            }
            return;
        }

        const uint64_t started = CleanupDeleteNowMs();
        const std::string threadId = CurrentThreadIdString();
        if (callbacks_.onItemStarted) {
            callbacks_.onItemStarted(path, "delete_file_parallel", started, threadId);
        }

        std::error_code ec;
        const bool removed = std::filesystem::remove(toLongPath(path), ec);
        const uint64_t elapsed = CleanupDeleteNowMs() - started;

        if (callbacks_.onItemFinished) {
            callbacks_.onItemFinished(path, ec, removed, elapsed, threadId);
        }
    }

    CleanupDeleteWorkload workload_;
    uint32_t concurrency_;
    CleanupDeleteCallbacks callbacks_;
    std::mutex startMutex_;
    bool started_ = false;
    std::unique_ptr<InstallerTaskManager> taskManager_;
};

CleanupDeleteExecutor::CleanupDeleteExecutor(CleanupDeleteWorkload workload,
                                             CleanupDeleteCallbacks callbacks)
    : impl_(std::make_unique<Impl>(workload, std::move(callbacks))) {}

CleanupDeleteExecutor::~CleanupDeleteExecutor() = default;

void CleanupDeleteExecutor::start() {
    impl_->start();
}

void CleanupDeleteExecutor::submit(std::vector<std::filesystem::path>& files) {
    impl_->submit(files);
}

void CleanupDeleteExecutor::finish() {
    impl_->finish();
}

uint32_t CleanupDeleteExecutor::workerConcurrency() const {
    return impl_->workerConcurrency();
}

} // namespace MultiThreadedInstaller
