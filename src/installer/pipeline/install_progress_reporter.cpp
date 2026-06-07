#include "installer/pipeline/install_progress_reporter.h"

#include "common/installer_logger.h"

#include <algorithm>

namespace MultiThreadedInstaller {

namespace {

constexpr float kPrecheckStart = 0.00f;
constexpr float kPrecheckEnd = 0.15f;
constexpr float kCleanupStart = 0.15f;
constexpr float kCleanupEnd = 0.35f;
constexpr float kInstallStart = 0.35f;
constexpr float kInstallEnd = 0.92f;
constexpr float kFinalizeStart = 0.92f;
constexpr float kFinalizeEnd = 1.00f;

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
        case InstallServicePhase::Installing:
            return kInstallStart + (kInstallEnd - kInstallStart) * clamped;
        case InstallServicePhase::Finalizing:
            return kFinalizeStart + (kFinalizeEnd - kFinalizeStart) * clamped;
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
        case InstallServicePhase::Installing:
            return "Installing";
        case InstallServicePhase::Finalizing:
            return "Finalizing";
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
