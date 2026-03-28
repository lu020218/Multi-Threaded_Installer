#pragma once

#include "installer/install_service.h"

#include <string>

namespace MultiThreadedInstaller {

const char* InstallServicePhaseName(InstallServicePhase phase);
const char* InstallServiceStatusName(InstallServiceStatus status);

class InstallProgressReporter {
public:
    explicit InstallProgressReporter(const InstallServiceCallbacks& callbacks);

    void EmitStatus(InstallServiceStatus status,
                    InstallServicePhase phase,
                    float phaseProgress,
                    const std::string& message);
    void EmitMessage(InstallServiceEventType type, const std::string& message);
    void EmitProgress(const std::string& folder,
                      const std::string& currentFile,
                      float phaseProgress);

    InstallServiceStatus CurrentStatus() const { return currentStatus_; }
    InstallServicePhase CurrentPhase() const { return currentPhase_; }
    float CurrentPhaseProgress() const { return currentPhaseProgress_; }
    float CurrentOverallProgress() const { return lastOverallProgress_; }

private:
    const InstallServiceCallbacks& callbacks_;
    InstallServiceStatus currentStatus_ = InstallServiceStatus::Preparing;
    InstallServicePhase currentPhase_ = InstallServicePhase::None;
    float currentPhaseProgress_ = 0.0f;
    float lastOverallProgress_ = 0.0f;
};

} // namespace MultiThreadedInstaller
