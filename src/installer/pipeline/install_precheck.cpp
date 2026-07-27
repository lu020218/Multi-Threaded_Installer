#include "installer/pipeline/install_precheck.h"

#include "common/engine_defaults.h"
#include "common/installer_logger.h"
#include "installer/state/install_state_utils.h"
#include "installer/platform/installer_helpers.h"

namespace MultiThreadedInstaller {

namespace {

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
}

std::string FormatWindowsVersion(uint16_t major, uint16_t minor, uint32_t build) {
    return std::to_string(major) + "." + std::to_string(minor) + "." +
           std::to_string(build);
}

} // namespace

bool ExecuteInstallPrecheck(const ExtendedInstallationMetadata& metadata,
                            const InstallExecutionPlan& plan,
                            const InstallServiceOptions& options,
                            InstallProgressReporter& reporter,
                            HANDLE& installMutex,
                            InstallerPathResolver& pathResolver,
                            std::string& error,
                            bool& cancelled) {
    error.clear();
    cancelled = false;

    // [R4] 覆盖/升级安装时，旧安装文件占用的空间会在安装过程中被替换释放，可计入"可用"；
    // 否则同根升级会把"需要 ≈ 新载荷大小"误判为需要额外的整份空间（过度保守）。
    // 另加一份 staging 余量，覆盖原子替换期间 <file>.__mti_new 与原文件同时存在的瞬时占用。
    uint64_t reclaimableBytes = 0;
    if (plan.pathDecision.mode != InstallTargetMode::FreshInstall &&
        plan.previousInstalledFingerprints) {
        for (const auto& entry : *plan.previousInstalledFingerprints) {
            reclaimableBytes += entry.second.size;
        }
    }
    constexpr uint64_t kStagingHeadroomBytes = 64ull * 1024ull * 1024ull;  // 64 MiB
    const uint64_t grossRequired = plan.totalInstallBytes + kStagingHeadroomBytes;
    const uint64_t effectiveRequired =
        grossRequired > reclaimableBytes ? grossRequired - reclaimableBytes : 0;

    uint64_t availableBytes = 0;
    if (!checkDiskSpaceForInstall(plan.pathDecision.diskCheckPath,
                                  effectiveRequired,
                                  availableBytes)) {
        error = "Insufficient disk space for installation. required=" +
                std::to_string(plan.totalInstallBytes) +
                " stagingHeadroom=" + std::to_string(kStagingHeadroomBytes) +
                " reclaimable=" + std::to_string(reclaimableBytes) +
                " effectiveRequired=" + std::to_string(effectiveRequired) +
                " available=" + std::to_string(availableBytes);
        return false;
    }
    reporter.EmitProgress("", "Disk space precheck", 0.25f);

#ifdef _WIN32
    uint16_t currentMajor = 0;
    uint16_t currentMinor = 0;
    uint32_t currentBuild = 0;
    if (!checkMinimumWindowsVersion(EngineDefaults::kMinWindowsMajor,
                                    EngineDefaults::kMinWindowsMinor,
                                    EngineDefaults::kMinWindowsBuild,
                                    currentMajor,
                                    currentMinor,
                                    currentBuild)) {
        const std::string required = FormatWindowsVersion(EngineDefaults::kMinWindowsMajor,
                                                          EngineDefaults::kMinWindowsMinor,
                                                          EngineDefaults::kMinWindowsBuild);
        const std::string current = FormatWindowsVersion(currentMajor,
                                                         currentMinor,
                                                         currentBuild);
        error = "Windows version does not meet minimum requirement. required=" +
                required + " current=" + current;
        logInstallerError("[InstallFlow][Precheck][OS] failed required=" +
                          required + " current=" + current);
        return false;
    }
    logInstallerInfo("[InstallFlow][Precheck][OS] passed required=" +
                     FormatWindowsVersion(EngineDefaults::kMinWindowsMajor,
                                          EngineDefaults::kMinWindowsMinor,
                                          EngineDefaults::kMinWindowsBuild) +
                     " current=" +
                     FormatWindowsVersion(currentMajor, currentMinor, currentBuild));
#endif
    reporter.EmitProgress("", "OS version precheck", 0.40f);

#ifdef _WIN32
    std::vector<std::string> processNames = buildKillProcessList(
        metadata.appName,
        plan.effectiveKillProcesses);
    if (!processNames.empty()) {
        std::vector<std::string> running = getRunningProcessesByName(processNames);
        if (!running.empty()) {
            std::string joined;
            for (size_t i = 0; i < running.size(); ++i) {
                if (i > 0) {
                    joined += ", ";
                }
                joined += running[i];
            }
            logInstallerInfo("[PROC] Attempting pre-install termination for: " + joined);
            terminateProcessesByName(running);
            Sleep(500);
            std::vector<std::string> remaining = getRunningProcessesByName(processNames);
            if (!remaining.empty()) {
                std::string unresolved;
                for (size_t i = 0; i < remaining.size(); ++i) {
                    if (i > 0) {
                        unresolved += ", ";
                    }
                    unresolved += remaining[i];
                }
                logInstallerWarning("[PROC] Processes still detected after terminate attempt: " +
                                    unresolved);
            }
        }
    }
#endif
    reporter.EmitProgress("", "Process precheck", 0.60f);

    if (IsCancellationRequested(options)) {
        cancelled = true;
        error = "Installation cancelled.";
        return false;
    }

    reporter.EmitStatus(InstallServiceStatus::Precheck,
                        InstallServicePhase::Precheck,
                        0.85f,
                        "Precheck almost complete...");

    if (EngineDefaults::kUseMutex) {
        reporter.EmitMessage(InstallServiceEventType::Info, "Acquiring install mutex...");
        installMutex = acquireInstallMutex(EngineDefaults::kUseMutex,
                                           EngineDefaults::MutexName(metadata.appProductName));
    }

    reporter.EmitProgress("", "Precheck completed", 1.0f);
    return true;
}

} // namespace MultiThreadedInstaller
