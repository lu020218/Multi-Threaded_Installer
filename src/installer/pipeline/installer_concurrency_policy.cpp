#include "installer/pipeline/installer_concurrency_policy.h"

#include <algorithm>
#include <thread>

namespace MultiThreadedInstaller {

uint32_t GetInstallerHardwareConcurrency() {
    const unsigned int hw = std::thread::hardware_concurrency();
    return hw == 0 ? 2u : static_cast<uint32_t>(hw);
}

uint32_t ResolvePayloadWorkerCount(size_t folderCount) {
    if (folderCount == 0) {
        return 0;
    }
    const uint32_t hw = GetInstallerHardwareConcurrency();
    const uint32_t budget = std::max<uint32_t>(1, hw / 2);
    return std::min<uint32_t>(static_cast<uint32_t>(folderCount), budget);
}

uint32_t ResolveDecoderThreadCount(uint32_t schedulerConcurrencyHint) {
    const uint32_t hw = GetInstallerHardwareConcurrency();
    const uint32_t safeSchedulerConcurrency = std::max<uint32_t>(1, schedulerConcurrencyHint);
    const uint32_t perPayloadBudget = std::max<uint32_t>(1, hw / safeSchedulerConcurrency);
    return std::min<uint32_t>(4, perPayloadBudget);
}

uint32_t ResolveCleanupDeleteConcurrency(CleanupDeleteWorkload workload) {
    (void)workload;
    const uint32_t hw = GetInstallerHardwareConcurrency();
    const uint32_t baseline = std::max<uint32_t>(2, hw / 2);
    return std::min<uint32_t>(4, baseline);
}

std::string CleanupDeleteWorkloadName(CleanupDeleteWorkload workload) {
    switch (workload) {
        case CleanupDeleteWorkload::Upgrade:
            return "upgrade";
        case CleanupDeleteWorkload::Uninstall:
            return "uninstall";
        default:
            return "unknown";
    }
}

} // namespace MultiThreadedInstaller
