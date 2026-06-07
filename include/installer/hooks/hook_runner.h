#pragma once

#include "common/archive_types.h"

#include <string>

namespace MultiThreadedInstaller {

// hook 执行结果。install_service 据此决定回滚或继续。
enum class HookOutcome {
    NotPresent,      // 该 hook 未配置，无操作
    Success,         // 脚本退出码 0
    FailedContinue,  // 失败但 onFailure=continue（记日志后继续）
    FailedAbort,     // 失败且 onFailure=abort（中止安装并回滚）
};

// 执行一个 pre/post 钩子（需求 §4 / 方案 §6）：
//   释放内嵌脚本到临时目录 → 注入 INSTALL_DIR/VERSION 环境变量 →
//   继承安装器管理员权限运行 → 等待最长 timeoutSec（超时则 kill）。
// 判据：退出码 0 = 成功；非 0 / 超时 = 失败 → 按 hook.onFailure 处理。
HookOutcome RunHook(const HookScript& hook,
                    const std::string& installDir,
                    const std::string& version);

} // namespace MultiThreadedInstaller
