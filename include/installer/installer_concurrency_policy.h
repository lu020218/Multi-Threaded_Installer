#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace MultiThreadedInstaller {

enum class CleanupDeleteWorkload {
    Upgrade,
    Uninstall,
};

uint32_t GetInstallerHardwareConcurrency();
uint32_t ResolvePayloadWorkerCount(size_t folderCount);
uint32_t ResolveDecoderThreadCount(uint32_t schedulerConcurrencyHint);
uint32_t ResolveCleanupDeleteConcurrency(CleanupDeleteWorkload workload);

std::string CleanupDeleteWorkloadName(CleanupDeleteWorkload workload);

} // namespace MultiThreadedInstaller
