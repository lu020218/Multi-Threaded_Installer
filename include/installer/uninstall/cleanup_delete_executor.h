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

/// 删除执行器的回调集合（用于进度上报与心跳/诊断）。
struct CleanupDeleteCallbacks {
    /// 开始删除一项时回调（path/动作/起始时间戳/线程标识）。
    std::function<void(const std::filesystem::path& path,
                       const std::string& action,
                       uint64_t startedMs,
                       const std::string& threadId)> onItemStarted;
    /// 删除一项结束时回调（path/错误码/是否真正删除/耗时/线程标识）。
    std::function<void(const std::filesystem::path& path,
                       const std::error_code& ec,
                       bool removed,
                       uint64_t elapsedMs,
                       const std::string& threadId)> onItemFinished;
    std::function<void(const std::filesystem::path& path)> onReparsePointSkipped;  ///< 跳过重解析点（符号链接等）时回调。
    std::function<void()> onEmptyPathSkipped;  ///< 跳过空路径时回调。
};

/// 并行删除执行器（pImpl）：把大批文件删除分发到多个工作线程，按 workload 决定并发度。
/// 通过 submit 分批投递、finish 等待全部完成；删除进展经 CleanupDeleteCallbacks 上报。
class CleanupDeleteExecutor {
public:
    CleanupDeleteExecutor(CleanupDeleteWorkload workload,
                          CleanupDeleteCallbacks callbacks);
    ~CleanupDeleteExecutor();

    CleanupDeleteExecutor(const CleanupDeleteExecutor&) = delete;
    CleanupDeleteExecutor& operator=(const CleanupDeleteExecutor&) = delete;

    void start();                                          ///< 启动工作线程。
    void submit(std::vector<std::filesystem::path>& files);///< 投递一批待删路径（内部接管/清空 files）。
    void finish();                                         ///< 等待全部删除完成并停止线程。

    uint32_t workerConcurrency() const;  ///< 实际工作线程数。

private:
    class Impl;
    std::unique_ptr<Impl> impl_;  ///< 实现细节（隐藏线程/队列）。
};

/// 单调毫秒时间戳（删除计时/心跳用）。
uint64_t CleanupDeleteNowMs();

} // namespace MultiThreadedInstaller
