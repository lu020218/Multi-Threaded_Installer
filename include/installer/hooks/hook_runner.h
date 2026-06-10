#pragma once

#include "common/archive_types.h"

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

// 执行一个 pre/post 钩子脚本（需求 §4 / 方案 §6）：
//   释放内嵌脚本到临时目录（保留原扩展名，支持 .bat/.cmd/.ps1）→ 注入
//   INSTALL_DIR/VERSION 环境变量 → 继承安装器管理员权限运行 →
//   等待最长 timeoutSec（超时则 kill）。
// 判据：退出码 0 = 成功；非 0 / 超时 = 失败 → 按 hook.onFailure 处理。
HookOutcome RunHook(const HookScript& hook,
                    const std::string& installDir,
                    const std::string& version);

// 依次执行一个钩子点的多个脚本（preInstall/postInstall 各可配置多个）：
//   按声明顺序逐个 RunHook；遇到 FailedAbort 立即停止并返回 FailedAbort（中止安装）；
//   FailedContinue 记录后继续后续脚本。聚合结果：
//   列表为空 → NotPresent；出现过 continue 失败 → FailedContinue；否则 Success。
HookOutcome RunHooks(const std::vector<HookScript>& hooks,
                     const std::string& installDir,
                     const std::string& version);

} // namespace MultiThreadedInstaller
