#include "installer/install_service.h"

#include "common/installer_logger.h"
#include "installer/install_cleanup_executor.h"
#include "installer/install_execution.h"
#include "installer/install_finalize.h"
#include "installer/install_plan_builder.h"
#include "installer/install_precheck.h"
#include "installer/install_progress_reporter.h"
#include "installer/install_state_utils.h"
#include "installer/installer_helpers.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

namespace {

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
}

} // namespace

InstallServiceResult ExecuteInstallService(const ExtendedInstallationMetadata& metadata,
                                           MetadataParser& parser,
                                           InstallerPathResolver& pathResolver,
                                           const InstallServiceOptions& options,
                                           const InstallServiceCallbacks& callbacks) {
    InstallServiceResult result;
    HANDLE installMutex = nullptr;
    bool installStateApplied = false;
    InstallProgressReporter reporter(callbacks);

    auto releaseResources = [&]() {
        if (installMutex) {
            releaseInstallMutex(installMutex);
            installMutex = nullptr;
        }
    };

    auto markFailed = [&](const std::string& message, bool cancelled, bool appendMessage) {
        result.success = false;
        result.cancelled = cancelled;
        if (appendMessage && !message.empty()) {
            result.errors.push_back(message);
        }
        const InstallServiceStatus failureStatus =
            cancelled ? InstallServiceStatus::Cancelled : InstallServiceStatus::Failed;
        if (!message.empty()) {
            reporter.EmitMessage(InstallServiceEventType::Error, message);
        }
        reporter.EmitStatus(failureStatus,
                            reporter.CurrentPhase(),
                            reporter.CurrentPhaseProgress(),
                            cancelled ? "Installation cancelled." : "Installation failed.");
        if (installStateApplied) {
            applyInstallState(metadata.installState, "failed", pathResolver);
            installStateApplied = false;
        }
        releaseResources();
    };

    try {
        reporter.EmitStatus(InstallServiceStatus::Preparing,
                            InstallServicePhase::None,
                            0.0f,
                            "Preparing installation...");

        if (IsCancellationRequested(options)) {
            markFailed("Installation cancelled.", true, true);
            return result;
        }

        reporter.EmitStatus(InstallServiceStatus::Precheck,
                            InstallServicePhase::Precheck,
                            0.0f,
                            "Running installation prechecks...");

        InstallExecutionPlan plan;
        std::string planError;
        if (!BuildInstallExecutionPlan(metadata, pathResolver, options, plan, planError)) {
            markFailed(planError.empty() ? "Failed to resolve install execution plan."
                                         : planError,
                       false,
                       true);
            return result;
        }

        logInstallerInfo(std::string("[InstallFlow][Plan] requestedPath=") + options.installPath +
                         " installPathExplicit=" +
                         (options.installPathExplicit ? "true" : "false") +
                         " cleanupOldInstallRequested=" +
                         (options.cleanupOldInstallRequested ? "true" : "false") +
                         " selectedComponents=" + std::to_string(options.selectedComponentIds.size()) +
                         " hasPreviousInstall=" + (plan.hasPreviousInstall ? "true" : "false"));
        logInstallerInfo(std::string("[InstallFlow][Path] mode=") +
                         InstallTargetModeName(plan.pathDecision.mode) +
                         " previousInstallDir=" + plan.previousInstallDir +
                         " requestedInstallRoot=" + plan.pathDecision.requestedInstallRoot +
                         " resolvedInstallRoot=" + plan.pathDecision.resolvedInstallRoot +
                         " cleanupTargetInstallRoot=" + plan.pathDecision.cleanupTargetInstallRoot +
                         " diskCheckPath=" + plan.pathDecision.diskCheckPath);
        logInstallerInfo(std::string("[InstallFlow][Plan] selectedEmbeddedFolders=") +
                         std::to_string(plan.selectedEmbeddedFolders.size()) +
                         " totalInstallBytes=" + std::to_string(plan.totalInstallBytes) +
                         " threadCount=" + std::to_string(options.threadCount));

        if (plan.componentPlan.hasComponents) {
            std::string selectedSummary = "Selected components:";
            if (plan.componentPlan.ordered.empty()) {
                selectedSummary += " (none)";
            } else {
                for (const auto* component : plan.componentPlan.ordered) {
                    selectedSummary += " ";
                    selectedSummary += component->id;
                }
            }
            reporter.EmitMessage(InstallServiceEventType::Info, selectedSummary);
        }

        std::string stageError;
        bool stageCancelled = false;
        if (!ExecuteInstallPrecheck(metadata,
                                    plan,
                                    options,
                                    reporter,
                                    installMutex,
                                    pathResolver,
                                    stageError,
                                    stageCancelled)) {
            markFailed(stageError.empty()
                           ? (stageCancelled ? "Installation cancelled." : "Installation precheck failed.")
                           : stageError,
                       stageCancelled,
                       true);
            return result;
        }

        if (!ExecuteInstallCleanup(metadata,
                                   plan,
                                   options,
                                   pathResolver,
                                   reporter,
                                   stageError,
                                   stageCancelled)) {
            markFailed(stageError.empty()
                           ? (stageCancelled ? "Installation cancelled." : "Installation cleanup failed.")
                           : stageError,
                       stageCancelled,
                       true);
            return result;
        }

        applyInstallState(metadata.installState, "installing", pathResolver);
        installStateApplied = true;

        InstallExecutionOutput executionOutput;
        if (!ExecuteInstallExecution(metadata,
                                     parser,
                                     plan,
                                     options,
                                     pathResolver,
                                     reporter,
                                     executionOutput)) {
            result.timing = executionOutput.timing;
            result.installRootPath = executionOutput.installRootPath;
            result.installedRoots = std::move(executionOutput.installedRoots);
            result.cancelled = executionOutput.cancelled;
            result.errors = std::move(executionOutput.errors);
            markFailed(std::string(), result.cancelled, false);
            return result;
        }

        result.timing = executionOutput.timing;
        result.installRootPath = executionOutput.installRootPath;
        result.installedRoots = std::move(executionOutput.installedRoots);
        result.cancelled = executionOutput.cancelled;

        if (!ExecuteInstallFinalization(metadata,
                                        plan,
                                        options,
                                        executionOutput.effectiveRegistry,
                                        executionOutput.effectiveKillProcesses,
                                        executionOutput.effectiveAutoStartup,
                                        executionOutput.effectiveDesktopIcons,
                                        executionOutput.componentActions,
                                        pathResolver,
                                        reporter,
                                        result)) {
            markFailed("Installation finalization failed.", false, true);
            return result;
        }
        installStateApplied = false;
        releaseResources();

        if (!executionOutput.failedOptionalComponentMessages.empty()) {
            result.success = false;
            result.errors.insert(result.errors.end(),
                                 executionOutput.failedOptionalComponentMessages.begin(),
                                 executionOutput.failedOptionalComponentMessages.end());
            reporter.EmitMessage(InstallServiceEventType::Error,
                                 "Installation completed with component failures.");
            reporter.EmitStatus(InstallServiceStatus::Failed,
                                InstallServicePhase::Finalizing,
                                1.0f,
                                "Installation completed with component failures.");
            logInstallerWarning(std::string("[InstallFlow][Done] success=false cancelled=") +
                                (result.cancelled ? "true" : "false") +
                                " errors=" + std::to_string(result.errors.size()) +
                                " installRootPath=" + result.installRootPath);
            return result;
        }

        result.success = true;
        reporter.EmitStatus(InstallServiceStatus::Completed,
                            InstallServicePhase::Finalizing,
                            1.0f,
                            "Installation completed.");
        logInstallerInfo(std::string("[InstallFlow][Done] success=true cancelled=") +
                         (result.cancelled ? "true" : "false") +
                         " errors=" + std::to_string(result.errors.size()) +
                         " installRootPath=" + result.installRootPath);
        return result;
    } catch (const std::exception& ex) {
        markFailed(ex.what(), IsCancellationRequested(options), true);
        return result;
    } catch (...) {
        markFailed("Unknown installation error.", IsCancellationRequested(options), true);
        return result;
    }
}

} // namespace MultiThreadedInstaller
