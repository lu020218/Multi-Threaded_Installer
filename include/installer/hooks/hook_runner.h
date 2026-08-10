#pragma once

#include "common/archive_types.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// hook 执行结果。install_service 据此决定回滚或继续。
enum class HookOutcome {
    NotPresent,      // 该 hook 未配置，无操作
    Success,         // 脚本退出码 0
    FailedContinue,  // 失败但 onFailure=continue（记日志后继续）
    FailedAbort,     // 失败且 onFailure=abort（中止安装并回滚）
};

// 单个 hook 的性能/结果明细（供 install_service 汇总到 TimingSummary，统一展示不分散）。
struct HookRunStat {
    std::string name;                    // 脚本名，如 post_install.bat
    std::string type;                    // 类型：bat/cmd | ps1 | msi | exe
    long long prepareMs = 0;             // 释放脚本+兄弟文件耗时
    long long execMs = 0;                // 子进程执行耗时
    unsigned long exitCode = 0;          // 退出码（超时/起不来时无意义）
    HookOutcome outcome = HookOutcome::NotPresent;  // 结果
};

// 执行一个 pre/post 钩子脚本（需求 §4 / 方案 §6）：
//   释放内嵌脚本到临时目录（保留原扩展名，支持 .bat/.cmd/.ps1）→ 注入
//   INSTALL_DIR/VERSION 环境变量 → 继承安装器管理员权限运行 →
//   等待最长 timeoutSec（超时则 kill）。
// 判据：退出码 0 = 成功；非 0 / 超时 = 失败 → 按 hook.onFailure 处理。
// outStat 非空时填入本次执行的性能/结果明细。
HookOutcome RunHook(const HookScript& hook,
                    const std::string& installDir,
                    const std::string& version,
                    HookRunStat* outStat = nullptr);

// 依次执行一个钩子点的多个脚本（preInstall/postInstall 各可配置多个）：
//   按声明顺序逐个 RunHook；遇到 FailedAbort 立即停止并返回 FailedAbort（中止安装）；
//   FailedContinue 记录后继续后续脚本。聚合结果：
//   列表为空 → NotPresent；出现过 continue 失败 → FailedContinue；否则 Success。
// outStats 非空时逐脚本追加其性能/结果明细。
// onProgress 非空时上报钩子点段内进度 [0..1]：按脚本数细分为基线，单个脚本执行期间
// 以时间脉冲渐近推进（p = t/(t+τ)，上限 0.95，脚本结束落到真实边界），
// 使耗时十几秒的脚本不再表现为进度条冻结。
HookOutcome RunHooks(const std::vector<HookScript>& hooks,
                     const std::string& installDir,
                     const std::string& version,
                     std::vector<HookRunStat>* outStats = nullptr,
                     const std::function<void(float fraction)>& onProgress = {});

} // namespace MultiThreadedInstaller
