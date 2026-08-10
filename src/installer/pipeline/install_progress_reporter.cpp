#include "installer/pipeline/install_progress_reporter.h"

#include "common/installer_logger.h"

#include <algorithm>
#include <mutex>

namespace MultiThreadedInstaller {

namespace {

// 全流程按执行顺序划分为 7 段，段内进度线性映射到总区间；单调不回退（见 EmitStatus）。
// 100% 只在最后一段 PostInstallHook 结束（或最终 Completed 事件）时到达 —— 保证“全部完成才 100%”。
constexpr float kPrecheckStart = 0.00f;
constexpr float kPrecheckEnd = 0.08f;
constexpr float kCleanupStart = 0.08f;
constexpr float kCleanupEnd = 0.18f;
constexpr float kPreHookStart = 0.18f;
constexpr float kPreHookEnd = 0.25f;
constexpr float kInstallStart = 0.25f;
constexpr float kInstallEnd = 0.70f;
constexpr float kComponentsStart = 0.70f;
constexpr float kComponentsEnd = 0.85f;
constexpr float kFinalizeStart = 0.85f;
constexpr float kFinalizeEnd = 0.92f;
constexpr float kPostHookStart = 0.92f;
constexpr float kPostHookEnd = 1.00f;

float Clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float ToOverallProgress(InstallServicePhase phase, float phaseProgress) {
    const float clamped = Clamp01(phaseProgress);
    switch (phase) {
        case InstallServicePhase::Precheck:
            return kPrecheckStart + (kPrecheckEnd - kPrecheckStart) * clamped;
        case InstallServicePhase::CleanupOldInstall:
            return kCleanupStart + (kCleanupEnd - kCleanupStart) * clamped;
        case InstallServicePhase::PreInstallHook:
            return kPreHookStart + (kPreHookEnd - kPreHookStart) * clamped;
        case InstallServicePhase::Installing:
            return kInstallStart + (kInstallEnd - kInstallStart) * clamped;
        case InstallServicePhase::Components:
            return kComponentsStart + (kComponentsEnd - kComponentsStart) * clamped;
        case InstallServicePhase::Finalizing:
            return kFinalizeStart + (kFinalizeEnd - kFinalizeStart) * clamped;
        case InstallServicePhase::PostInstallHook:
            return kPostHookStart + (kPostHookEnd - kPostHookStart) * clamped;
        case InstallServicePhase::None:
        default:
            return 0.0f;
    }
}

void EmitEvent(const InstallServiceCallbacks& callbacks, const InstallServiceEvent& event) {
    if (callbacks.onEvent) {
        callbacks.onEvent(event);
    }
}

} // namespace

const char* InstallServicePhaseName(InstallServicePhase phase) {
    switch (phase) {
        case InstallServicePhase::Precheck:
            return "Precheck";
        case InstallServicePhase::CleanupOldInstall:
            return "CleanupOldInstall";
        case InstallServicePhase::PreInstallHook:
            return "PreInstallHook";
        case InstallServicePhase::Installing:
            return "Installing";
        case InstallServicePhase::Components:
            return "Components";
        case InstallServicePhase::Finalizing:
            return "Finalizing";
        case InstallServicePhase::PostInstallHook:
            return "PostInstallHook";
        case InstallServicePhase::None:
        default:
            return "None";
    }
}

const char* InstallServiceStatusName(InstallServiceStatus status) {
    switch (status) {
        case InstallServiceStatus::Preparing:
            return "Preparing";
        case InstallServiceStatus::Precheck:
            return "Precheck";
        case InstallServiceStatus::Installing:
            return "Installing";
        case InstallServiceStatus::Finalizing:
            return "Finalizing";
        case InstallServiceStatus::Completed:
            return "Completed";
        case InstallServiceStatus::RebootRequired:
            return "RebootRequired";
        case InstallServiceStatus::Failed:
            return "Failed";
        case InstallServiceStatus::Cancelled:
            return "Cancelled";
        default:
            return "Unknown";
    }
}

InstallProgressReporter::InstallProgressReporter(const InstallServiceCallbacks& callbacks)
    : callbacks_(callbacks) {}

void InstallProgressReporter::EmitStatus(InstallServiceStatus status,
                                         InstallServicePhase phase,
                                         float phaseProgress,
                                         const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentStatus_ = status;
    currentPhase_ = phase;
    currentPhaseProgress_ = Clamp01(phaseProgress);
    float overall = ToOverallProgress(currentPhase_, currentPhaseProgress_);
    if (overall < lastOverallProgress_) {
        overall = lastOverallProgress_;
    }
    lastOverallProgress_ = Clamp01(overall);

    InstallServiceEvent event;
    event.type = InstallServiceEventType::Status;
    event.status = currentStatus_;
    event.phase = currentPhase_;
    event.phaseProgress = currentPhaseProgress_;
    event.overallProgress = lastOverallProgress_;
    event.progress = event.overallProgress;
    event.message = message;
    EmitEvent(callbacks_, event);

    logInstallerDebug(std::string("[InstallFlow][Status] status=") +
                      InstallServiceStatusName(currentStatus_) +
                      " phase=" + InstallServicePhaseName(currentPhase_) +
                      " phaseProgress=" + std::to_string(currentPhaseProgress_) +
                      " overall=" + std::to_string(lastOverallProgress_) +
                      " message=" + message);
}

void InstallProgressReporter::EmitMessage(InstallServiceEventType type, const std::string& message) {
    if (message.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);

    InstallServiceEvent event;
    event.type = type;
    event.status = currentStatus_;
    event.phase = currentPhase_;
    event.phaseProgress = currentPhaseProgress_;
    event.overallProgress = lastOverallProgress_;
    event.progress = event.overallProgress;
    event.message = message;
    EmitEvent(callbacks_, event);
}

void InstallProgressReporter::EmitProgress(const std::string& folder,
                                           const std::string& currentFile,
                                           float phaseProgress) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentPhaseProgress_ = std::max(currentPhaseProgress_, Clamp01(phaseProgress));
    float overall = ToOverallProgress(currentPhase_, currentPhaseProgress_);
    if (overall < lastOverallProgress_) {
        overall = lastOverallProgress_;
    }
    lastOverallProgress_ = Clamp01(overall);

    InstallServiceEvent event;
    event.type = InstallServiceEventType::Progress;
    event.status = currentStatus_;
    event.phase = currentPhase_;
    event.folder = folder;
    event.currentFile = currentFile;
    event.phaseProgress = currentPhaseProgress_;
    event.overallProgress = lastOverallProgress_;
    event.progress = event.overallProgress;
    EmitEvent(callbacks_, event);
}

} // namespace MultiThreadedInstaller
