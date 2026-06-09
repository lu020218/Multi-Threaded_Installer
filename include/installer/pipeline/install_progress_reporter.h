#pragma once

#include "installer/pipeline/install_service.h"

#include <string>

namespace MultiThreadedInstaller {

const char* InstallServicePhaseName(InstallServicePhase phase);    ///< 阶段枚举 → 可读名（日志用）。
const char* InstallServiceStatusName(InstallServiceStatus status); ///< 状态枚举 → 可读名（日志用）。

/// 进度上报器：把各阶段的状态/进度/日志归一为 InstallServiceEvent，按"阶段进度→总体进度"
/// 的权重换算后通过回调上报。同时记录当前阶段/进度，供 markFailed 等复用。
class InstallProgressReporter {
public:
    explicit InstallProgressReporter(const InstallServiceCallbacks& callbacks);

    /// 发一条状态变更事件（进入新阶段 / 完成 / 失败等）。
    void EmitStatus(InstallServiceStatus status,
                    InstallServicePhase phase,
                    float phaseProgress,
                    const std::string& message);
    /// 发一条信息/警告/错误日志事件。
    void EmitMessage(InstallServiceEventType type, const std::string& message);
    /// 发一条进度事件（当前 folder/文件 + 阶段内进度）。
    void EmitProgress(const std::string& folder,
                      const std::string& currentFile,
                      float phaseProgress);

    InstallServiceStatus CurrentStatus() const { return currentStatus_; }       ///< 当前状态。
    InstallServicePhase CurrentPhase() const { return currentPhase_; }          ///< 当前阶段。
    float CurrentPhaseProgress() const { return currentPhaseProgress_; }        ///< 当前阶段进度。
    float CurrentOverallProgress() const { return lastOverallProgress_; }       ///< 最近一次总体进度。

private:
    const InstallServiceCallbacks& callbacks_;  ///< 事件回调（外部持有）。
    InstallServiceStatus currentStatus_ = InstallServiceStatus::Preparing;  ///< 当前状态。
    InstallServicePhase currentPhase_ = InstallServicePhase::None;          ///< 当前阶段。
    float currentPhaseProgress_ = 0.0f;   ///< 当前阶段进度 [0..1]。
    float lastOverallProgress_ = 0.0f;    ///< 最近换算出的总体进度 [0..1]。
};

} // namespace MultiThreadedInstaller
