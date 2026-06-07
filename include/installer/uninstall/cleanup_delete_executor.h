#pragma once

#include "installer/pipeline/installer_concurrency_policy.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace MultiThreadedInstaller {

struct CleanupDeleteCallbacks {
    std::function<void(const std::filesystem::path& path,
                       const std::string& action,
                       uint64_t startedMs,
                       const std::string& threadId)> onItemStarted;
    std::function<void(const std::filesystem::path& path,
                       const std::error_code& ec,
                       bool removed,
                       uint64_t elapsedMs,
                       const std::string& threadId)> onItemFinished;
    std::function<void(const std::filesystem::path& path)> onReparsePointSkipped;
    std::function<void()> onEmptyPathSkipped;
};

class CleanupDeleteExecutor {
public:
    CleanupDeleteExecutor(CleanupDeleteWorkload workload,
                          CleanupDeleteCallbacks callbacks);
    ~CleanupDeleteExecutor();

    CleanupDeleteExecutor(const CleanupDeleteExecutor&) = delete;
    CleanupDeleteExecutor& operator=(const CleanupDeleteExecutor&) = delete;

    void start();
    void submit(std::vector<std::filesystem::path>& files);
    void finish();

    uint32_t workerConcurrency() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

uint64_t CleanupDeleteNowMs();

} // namespace MultiThreadedInstaller
