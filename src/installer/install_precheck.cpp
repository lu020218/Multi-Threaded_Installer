#include "installer/install_precheck.h"

#include "installer/install_state_utils.h"
#include "installer/installer_helpers.h"

namespace MultiThreadedInstaller {

namespace {

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
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

    uint64_t availableBytes = 0;
    if (!checkDiskSpaceForInstall(plan.pathDecision.diskCheckPath,
                                  plan.totalInstallBytes,
                                  availableBytes)) {
        error = "Insufficient disk space for installation. required=" +
                std::to_string(plan.totalInstallBytes) + " available=" +
                std::to_string(availableBytes);
        return false;
    }
    reporter.EmitProgress("", "Disk space precheck", 0.25f);

#ifdef _WIN32
    uint16_t currentMajor = 0;
    uint16_t currentMinor = 0;
    uint32_t currentBuild = 0;
    if (!checkMinimumWindowsVersion(metadata.minWindowsMajor,
                                    metadata.minWindowsMinor,
                                    metadata.minWindowsBuild,
                                    currentMajor,
                                    currentMinor,
                                    currentBuild)) {
        error = "Windows version does not meet minimum requirement.";
        return false;
    }
#endif
    reporter.EmitProgress("", "OS version precheck", 0.40f);

#ifdef _WIN32
    std::vector<std::string> processNames = buildKillProcessList(
        metadata.applicationName,
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
            reporter.EmitMessage(InstallServiceEventType::Info, "Terminating processes: " + joined);
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
                error = "Failed to terminate processes: " + unresolved;
                return false;
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

    if (metadata.installState.useMutex) {
        reporter.EmitMessage(InstallServiceEventType::Info, "Acquiring install mutex...");
        installMutex = acquireInstallMutex(metadata.installState);
    }

    reporter.EmitProgress("", "Precheck completed", 1.0f);
    return true;
}

} // namespace MultiThreadedInstaller
