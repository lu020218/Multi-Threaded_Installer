#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace MultiThreadedInstaller {

// 并发策略：把"该开多少线程"的决策集中在此，依据 CPU 核数与负载类型给出建议值。

/// 删除型清理的负载类型（不同场景用不同并发上限）。
enum class CleanupDeleteWorkload {
    Upgrade,    ///< 升级时清理旧安装。
    Uninstall,  ///< 卸载时删除已装文件。
};

/// 当前机器的硬件并发度（std::thread::hardware_concurrency，0 时回退 1）。
uint32_t GetInstallerHardwareConcurrency();
/// 解压阶段的 folder 级工作线程数（按 folder 数与 CPU 取折中）。
uint32_t ResolvePayloadWorkerCount(size_t folderCount);
/// 单个 folder 解压时分给解码器的线程数（按调度并发提示与 CPU 推算）。
uint32_t ResolveDecoderThreadCount(uint32_t schedulerConcurrencyHint);
/// 删除型清理的并发线程数（按负载类型）。
uint32_t ResolveCleanupDeleteConcurrency(CleanupDeleteWorkload workload);

/// 负载类型 → 可读名（日志用）。
std::string CleanupDeleteWorkloadName(CleanupDeleteWorkload workload);

} // namespace MultiThreadedInstaller
