#include "installer/install_cleanup_executor.h"

#include "common/installer_logger.h"
#include "installer/console_interface.h"
#include "installer/installer_helpers.h"
#include "installer/upgrade_cleanup.h"

namespace MultiThreadedInstaller {

namespace {

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
}

} // namespace

bool ExecuteInstallCleanup(const ExtendedInstallationMetadata& metadata,
                           const InstallExecutionPlan& plan,
                           const InstallServiceOptions& options,
                           InstallerPathResolver& pathResolver,
                           InstallProgressReporter& reporter,
                           std::string& error,
                           bool& cancelled) {
    error.clear();
    cancelled = false;

    if (!plan.hasPreviousInstall) {
        return true;
    }

    reporter.EmitMessage(InstallServiceEventType::Info,
                         "Detected previous install at: " + plan.previousInstallDir);

    logInstallerInfo(std::string("[InstallFlow][Cleanup] start previousManifest=") +
                     plan.previousManifest + " previousInstallDir=" + plan.previousInstallDir +
                     " resolvedInstallRoot=" + plan.pathDecision.resolvedInstallRoot +
                     " targetInstallRoot=" + plan.pathDecision.cleanupTargetInstallRoot);
    reporter.EmitStatus(InstallServiceStatus::Precheck,
                        InstallServicePhase::CleanupOldInstall,
                        0.0f,
                        "Cleaning previous installation...");

    CliSupport console;
    auto cleanupProgress = [&](const UpgradeCleanupProgressInfo& info) {
        const std::string detail = info.currentItem.empty()
                                       ? std::string("Cleaning previous installation")
                                       : info.currentItem;
        reporter.EmitProgress("cleanup", detail, info.progress);
    };

    if (!cleanupPreviousInstallForUpgrade(plan.previousManifest,
                                          plan.previousInstallDir,
                                          plan.pathDecision.resolvedInstallRoot,
                                          console,
                                          cleanupProgress,
                                          options.cancellationCallback)) {
        if (IsCancellationRequested(options)) {
            cancelled = true;
            error = "Installation cancelled.";
            return false;
        }
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Previous install cleanup reported failure.");
        logInstallerWarning("[InstallFlow][Cleanup] finished with failure");
    } else {
        logInstallerInfo("[InstallFlow][Cleanup] finished successfully");
    }

    if (!cleanupUpgradeSystemArtifacts(plan.previousManifest,
                                      plan.previousInstallDir,
                                      metadata,
                                      pathResolver,
                                      console,
                                      cleanupProgress,
                                      options.cancellationCallback)) {
        if (IsCancellationRequested(options)) {
            cancelled = true;
            error = "Installation cancelled.";
            return false;
        }
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Previous install system cleanup reported failure.");
        logInstallerWarning("[InstallFlow][Cleanup] system cleanup finished with failure");
    } else {
        logInstallerInfo("[InstallFlow][Cleanup] system cleanup finished successfully");
    }

    reporter.EmitProgress("cleanup", "Previous installation cleanup finished", 1.0f);
    return true;
}

} // namespace MultiThreadedInstaller
