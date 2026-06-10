#pragma once

#include "common/config_types.h"

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// ---------------------------------------------------------------------------
// 引擎私有的运行时记账型（值/逻辑归位后的运行期半边）。
//
// 这些结构曾经由 YAML 填充，重构后**不再来自 YAML**：它们是引擎在安装时
// "记下装了什么、卸载时要撤销什么"的内部账本，以及写注册表的目标原语。
// 与 config_types.h 里删掉的同名 YAML 配置型彻底分家，避免再次混淆"值/逻辑"。
//
// 跨版本兼容（旧名单收尾）一律走 installer/migration 的迁移表，不在这里。
// ---------------------------------------------------------------------------

// 写注册表的目标原语：引擎拼好后交给 registry_utils 落盘。
struct RegistryEntry {
    std::string path;
    std::string key;
    std::string value;
    RegistryValueType type = RegistryValueType::STRING;
};

// 注册表只读探测目标（读已装版本/安装目录等）。
struct RegistryLookupEntry {
    std::string path;
    std::string key;
};

// 一条系统卸载入口收尾项（按 DisplayName 在指定 hive 删除）。
struct UninstallEntryCleanup {
    std::string name;
    UninstallEntryScope scope = UninstallEntryScope::ANY;
};

// 仅带名字的清理项（进程名 / 快捷方式名 / 启动项名）。
struct NamedCleanupEntry {
    std::string name;
};

// 一条路径清理规则。
struct UninstallCleanupRule {
    std::string path;
    bool recursive = true;
    bool onlyIfEmpty = false;
};

// 运行时卸载账本：install_finalize 记录、写入 install.manifest.json，卸载时读取并撤销。
// 注册表与系统卸载入口的清理已由引擎按产品名写死处理，故不再记录注册表/路径名单。
struct UninstallCleanupConfig {
    std::vector<NamedCleanupEntry> processes;             // 卸载前需结束的进程
    std::vector<UninstallEntryCleanup> uninstallEntries;  // 要删除的系统卸载入口
    std::vector<NamedCleanupEntry> shortcuts;             // 要删除的快捷方式
    std::vector<NamedCleanupEntry> startup;               // 要删除的开机自启项
};

} // namespace MultiThreadedInstaller
