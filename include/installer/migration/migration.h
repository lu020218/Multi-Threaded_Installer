#pragma once

#include <string>
#include <vector>

namespace MultiThreadedInstaller {
namespace migration {

// ---------------------------------------------------------------------------
// 引擎内的跨版本迁移表（需求 §6 / 方案 §5）。
//
// 跨版本兼容是"读旧状态→判断→分支"的逻辑，**不**用 YAML legacy 名单或 bat 承载。
// 旧名单作为字面量留在迁移代码里：它们跟版本走，到某版本无用户即可连函数一起删除，
// 不会像 YAML legacy 名单那样越积越多无人敢删。
// ---------------------------------------------------------------------------

struct MigrationContext {
    std::string installDir;   // 本次安装目录
    std::string productName;  // 写死的产品名（数据目录/注册表键）
    std::string fromVersion;  // 已安装版本（注册表读出，可能为空=全新安装）
    std::string toVersion;    // 本次安装版本
};

// 单个迁移：从 < atVersion 升级到 >= atVersion 时执行一次。
struct Migration {
    const char* atVersion;                   // 版本节点，如 "7.0.0"
    bool (*apply)(MigrationContext& ctx);    // 返回 false=失败（按 abort 处理）
};

// 迁移表：按版本升序排列，跟版本走，淘汰随版本删除。
const std::vector<Migration>& Registry();

// 执行：挑出 atVersion > fromVersion 的迁移按序执行，逐条记录完成状态。
// fromVersion 为空（全新安装）时所有迁移都跳过——全新安装无旧状态可迁移。
bool RunPending(MigrationContext& ctx);

} // namespace migration
} // namespace MultiThreadedInstaller
