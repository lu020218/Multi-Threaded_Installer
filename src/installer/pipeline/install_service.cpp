#include "installer/pipeline/install_service.h"

#include "common/installer_logger.h"
#include "installer/hooks/hook_runner.h"
#include "installer/pipeline/install_cleanup_executor.h"
#include "installer/pipeline/install_execution.h"
#include "installer/pipeline/install_finalize.h"
#include "installer/pipeline/install_plan_builder.h"
#include "installer/pipeline/install_precheck.h"
#include "installer/pipeline/install_progress_reporter.h"
#include "installer/state/install_state_utils.h"
#include "installer/state/install_state_store.h"
#include "installer/platform/installer_helpers.h"
#include "installer/state/registry_utils.h"
#include "common/utf8_utils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

namespace {

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
}

using SteadyTimePoint = std::chrono::steady_clock::time_point;

uint64_t ElapsedMs(SteadyTimePoint start) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
}

struct InstallFlowTiming {
    SteadyTimePoint totalStart = std::chrono::steady_clock::now();
    uint64_t planMs = 0;
    uint64_t precheckMs = 0;
    uint64_t cleanupMs = 0;
    uint64_t executeMs = 0;
    uint64_t finalizeMs = 0;
    ParallelInstallSummary payload;
    std::vector<ComponentInstallTiming> components;
};

InstallStateContext BuildServiceInstallStateContext(const ExtendedInstallationMetadata& metadata,
                                                    const InstallExecutionPlan& plan,
                                                    const InstallServiceOptions& options,
                                                    const std::string& state) {
    InstallStateContext context;
    context.installDir = plan.pathDecision.resolvedInstallRoot;
    context.version = metadata.appVersion;
    context.appName = metadata.appProductName;
    context.appId = plan.effectiveAppId.empty() ? metadata.appProductName : plan.effectiveAppId;
    context.installSource = getCurrentExecutablePath();
    context.state = state;
    context.userName = GetCurrentUserNameForInstallState();
    if (context.installDir.empty()) {
        context.installDir = options.installPath;
    }
    return context;
}

// postInstall 钩子以 abort 失败时，本次安装在 finalize 之后被判失败 —— 需把"已装好"的
// 产物撤销，恢复到安装前状态，让 abort 在 post 阶段也名副其实（与 §6.2 一致）。
// 尽力而为：逐项删除并记日志，单项失败不阻断其余清理。
void RollbackInstalledArtifacts(const ExtendedInstallationMetadata& metadata,
                                const InstallExecutionPlan& plan,
                                const InstallServiceOptions& options,
                                const InstallServiceResult& result,
                                InstallerPathResolver& pathResolver,
                                InstallProgressReporter& reporter) {
    reporter.EmitMessage(InstallServiceEventType::Warning,
                         "Rolling back installation after postInstall abort...");

    auto removeFile = [](const std::string& path) {
        if (path.empty()) {
            return;
        }
        std::error_code ec;
        std::filesystem::remove(PathFromUtf8(path), ec);
    };

    // 1) 删已写入的文件（含 manifest 与 uninstall.exe —— 此刻均无进程占用）。
    for (const auto& file : result.installedFiles) {
        removeFile(file);
    }
    removeFile(result.uninstallPath);
    if (!result.installRootPath.empty()) {
        removeFile(Utf8FromPath(PathFromUtf8(result.installRootPath) / "install.manifest.json"));
    }

    // 2) 删安装根目录（best-effort：非空/被占用则保留，记日志）。
    for (const auto& root : result.installedRoots) {
        if (root.empty()) {
            continue;
        }
        std::error_code ec;
        std::filesystem::remove_all(PathFromUtf8(root), ec);
        if (ec) {
            logInstallerWarning("[Rollback] Failed to remove install root: " + root +
                                " error=" + ec.message());
        }
    }

    // 3) 删产品注册表 + 系统卸载入口。
    deleteUninstallRegistryEntry(metadata.appProductName, /*perMachine=*/true);
    deleteUninstallRegistryEntry(metadata.appProductName, /*perMachine=*/false);
    deleteSystemUninstallEntryByDisplayName(metadata.appProductName, UninstallEntryScope::ANY);

    // 4) 删 install-state（HKLM\Software\<product> + install-state.json）。
    CleanupInstallState("delete",
                        BuildServiceInstallStateContext(metadata, plan, options, "uninstalled"),
                        pathResolver);

    logInstallerInfo("[Rollback] postInstall abort rollback completed.");
}

} // namespace

InstallServiceResult ExecuteInstallService(const ExtendedInstallationMetadata& metadata,
                                           MetadataParser& parser,
                                           InstallerPathResolver& pathResolver,
                                           const InstallServiceOptions& options,
                                           const InstallServiceCallbacks& callbacks) {
    InstallServiceResult result;
    HANDLE installMutex = nullptr;
    bool coreInstallInfoApplied = false;
    InstallProgressReporter reporter(callbacks);
    InstallExecutionPlan plan;
    InstallFlowTiming flowTiming;

    auto releaseResources = [&]() {
        if (installMutex) {
            releaseInstallMutex(installMutex);
            installMutex = nullptr;
        }
    };

    auto logTimingSummary = [&]() {
        std::ostringstream oss;
        oss << "[InstallFlow][TimingSummary]\n"
            << "  result: success=" << (result.success ? "true" : "false")
            << " cancelled=" << (result.cancelled ? "true" : "false")
            << " rebootRequired=" << (result.rebootRequired ? "true" : "false")
            << " errors=" << result.errors.size()
            << " installRootPath=" << result.installRootPath << "\n"
            << "  total: " << ElapsedMs(flowTiming.totalStart) << "ms\n"
            << "  stages: plan=" << flowTiming.planMs
            << "ms precheck=" << flowTiming.precheckMs
            << "ms cleanup=" << flowTiming.cleanupMs
            << "ms execute=" << flowTiming.executeMs
            << "ms finalize=" << flowTiming.finalizeMs << "ms\n"
            << "  payload: total=" << flowTiming.payload.totalSec
            << "s read=" << flowTiming.payload.payloadReadSec
            << "s decompress=" << flowTiming.payload.decompressSec
            << "s write=" << flowTiming.payload.writeSec
            << "s folders=" << flowTiming.payload.folderTimings.size() << "\n";
        if (flowTiming.components.empty()) {
            oss << "  components: none";
        } else {
            oss << "  components:";
            for (const auto& component : flowTiming.components) {
                oss << "\n    - id=" << component.id
                    << " name=" << std::quoted(component.name)
                    << " type=" << component.type
                    << " success=" << (component.success ? "true" : "false")
                    << " total=" << component.totalMs << "ms";
                if (component.type == "download") {
                    oss << " download=" << component.downloadMs
                        << "ms install=" << component.installMs << "ms";
                }
                if (!component.success && !component.error.empty()) {
                    oss << " error=" << std::quoted(component.error);
                }
            }
        }
        logInstallerInfo(oss.str());
    };

    auto finishResult = [&]() -> InstallServiceResult {
        logTimingSummary();
        return result;
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
        if (coreInstallInfoApplied) {
            ApplyInstallState(BuildServiceInstallStateContext(metadata, plan, options, "install_failed"),
                              pathResolver);
            coreInstallInfoApplied = false;
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
            return finishResult();
        }

        reporter.EmitStatus(InstallServiceStatus::Precheck,
                            InstallServicePhase::Precheck,
                            0.0f,
                            "Running installation prechecks...");

        std::string planError;
        auto planStart = std::chrono::steady_clock::now();
        if (!BuildInstallExecutionPlan(metadata, pathResolver, options, plan, planError)) {
            flowTiming.planMs = ElapsedMs(planStart);
            markFailed(planError.empty() ? "Failed to resolve install execution plan."
                                         : planError,
                       false,
                       true);
            return finishResult();
        }
        flowTiming.planMs = ElapsedMs(planStart);

        logInstallerInfo(std::string("[InstallFlow][Plan] requestedPath=") + options.installPath +
                         " installPathExplicit=" +
                         (options.installPathExplicit ? "true" : "false") +
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
                         " totalInstallBytes=" + std::to_string(plan.totalInstallBytes));

        std::string stageError;
        bool stageCancelled = false;
        auto precheckStart = std::chrono::steady_clock::now();
        if (!ExecuteInstallPrecheck(metadata,
                                    plan,
                                    options,
                                    reporter,
                                    installMutex,
                                    pathResolver,
                                    stageError,
                                    stageCancelled)) {
            flowTiming.precheckMs = ElapsedMs(precheckStart);
            markFailed(stageError.empty()
                           ? (stageCancelled ? "Installation cancelled." : "Installation precheck failed.")
                           : stageError,
                       stageCancelled,
                       true);
            return finishResult();
        }
        flowTiming.precheckMs = ElapsedMs(precheckStart);

        auto cleanupStart = std::chrono::steady_clock::now();
        if (!ExecuteInstallCleanup(metadata,
                                   plan,
                                   options,
                                   pathResolver,
                                   reporter,
                                   stageError,
                                   stageCancelled)) {
            flowTiming.cleanupMs = ElapsedMs(cleanupStart);
            markFailed(stageError.empty()
                           ? (stageCancelled ? "Installation cancelled." : "Installation cleanup failed.")
                           : stageError,
                       stageCancelled,
                       true);
            return finishResult();
        }
        flowTiming.cleanupMs = ElapsedMs(cleanupStart);

        ApplyInstallState(BuildServiceInstallStateContext(metadata, plan, options, "installing"),
                          pathResolver);
        coreInstallInfoApplied = true;

        const std::string hookInstallDir = plan.pathDecision.resolvedInstallRoot.empty()
                                               ? options.installPath
                                               : plan.pathDecision.resolvedInstallRoot;

        // preInstall hook：在解压前执行（需求 §4 时序）。失败按 onFailure=abort 中止回滚。
        if (RunHook(metadata.preInstall, hookInstallDir, metadata.appVersion) ==
            HookOutcome::FailedAbort) {
            markFailed("preInstall hook failed; installation aborted.", false, true);
            return finishResult();
        }

        InstallExecutionOutput executionOutput;
        auto executeStart = std::chrono::steady_clock::now();
        if (!ExecuteInstallExecution(metadata,
                                     parser,
                                     plan,
                                     options,
                                     pathResolver,
                                     reporter,
                                     executionOutput)) {
            flowTiming.executeMs = ElapsedMs(executeStart);
            result.timing = executionOutput.timing;
            flowTiming.payload = executionOutput.timing;
            flowTiming.components = executionOutput.componentTimings;
            result.installRootPath = executionOutput.installRootPath;
            result.installedRoots = std::move(executionOutput.installedRoots);
            result.cancelled = executionOutput.cancelled;
            result.errors = std::move(executionOutput.errors);
            markFailed(std::string(), result.cancelled, false);
            return finishResult();
        }
        flowTiming.executeMs = ElapsedMs(executeStart);

        result.timing = executionOutput.timing;
        flowTiming.payload = executionOutput.timing;
        flowTiming.components = executionOutput.componentTimings;
        result.installRootPath = executionOutput.installRootPath;
        result.installedRoots = std::move(executionOutput.installedRoots);
        result.installedFiles = std::move(executionOutput.installedFiles);
        result.cancelled = executionOutput.cancelled;
        result.rebootRequired = executionOutput.rebootRequired;
        result.pendingReplaceFiles = std::move(executionOutput.pendingReplaceFiles);

        auto finalizeStart = std::chrono::steady_clock::now();
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
            flowTiming.finalizeMs = ElapsedMs(finalizeStart);
            markFailed("Installation finalization failed.", false, true);
            return finishResult();
        }
        flowTiming.finalizeMs = ElapsedMs(finalizeStart);
        coreInstallInfoApplied = false;

        // postInstall hook：在 finalize 后执行（需求 §4 时序）。
        const std::string postHookInstallDir =
            result.installRootPath.empty() ? hookInstallDir : result.installRootPath;
        if (RunHook(metadata.postInstall, postHookInstallDir, metadata.appVersion) ==
            HookOutcome::FailedAbort) {
            RollbackInstalledArtifacts(metadata, plan, options, result, pathResolver, reporter);
            markFailed("postInstall hook failed; installation rolled back.", false, true);
            return finishResult();
        }

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
            return finishResult();
        }

        if (result.rebootRequired) {
            result.success = false;
            reporter.EmitStatus(InstallServiceStatus::RebootRequired,
                                InstallServicePhase::Finalizing,
                                1.0f,
                                "Installation requires a system reboot to finish replacing locked files.");
            reporter.EmitMessage(InstallServiceEventType::Warning,
                                 "Some files were scheduled for replacement after reboot.");
            logInstallerWarning(std::string("[InstallFlow][Done] rebootRequired=true cancelled=") +
                                (result.cancelled ? "true" : "false") +
                                " pendingReplaceFiles=" +
                                std::to_string(result.pendingReplaceFiles.size()) +
                                " installRootPath=" + result.installRootPath);
            return finishResult();
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
        return finishResult();
    } catch (const std::exception& ex) {
        markFailed(ex.what(), IsCancellationRequested(options), true);
        return finishResult();
    } catch (...) {
        markFailed("Unknown installation error.", IsCancellationRequested(options), true);
        return finishResult();
    }
}

} // namespace MultiThreadedInstaller
