#pragma once

#include "common/config_types.h"
#include "installer/state/uninstall_record.h"
#include <string>
#include <vector>

// 本头的公开 API 不暴露任何 Win32 类型；<windows.h> 留在 .cpp 内包含。

namespace MultiThreadedInstaller {

// 注册表读写原语。路径形如 "HKEY_LOCAL_MACHINE\\Software\\..." 或简写 "HKLM\\..."/"HKCU\\..."。
// 全部为 best-effort：失败返回 false 并由调用方记日志，不抛异常。

/// 删除一个注册表值（按 entry.path + entry.key）。用于卸载时清理安装期写入的自定义注册表项。
bool deleteRegistryValue(const RegistryEntry& entry);
/// 递归删除一个注册表键（整棵子树）。
bool deleteRegistryPath(const std::string& path);
/// 写一个注册表值（类型由 type 决定：STRING/DWORD/EXPAND_STRING）。
bool writeRegistryValue(const RegistryEntry& entry, const std::string& value, RegistryValueType type);
/// 批量写注册表值，写入前对 value 中的 %InstallDir%/%Version%/%AppName% 占位符做替换。
void applyRegistryEntries(const std::vector<RegistryEntry>& entries,
                          const std::string& installDir,
                          const std::string& configVersion,
                          const std::string& appName);
/// 把任意名字净化成合法的注册表子键名（替换非法字符）。
std::string sanitizeRegistryKeyName(const std::string& name);
/// 读一个字符串型注册表值（REG_SZ/REG_EXPAND_SZ，后者自动展开环境变量）。成功置 value 返回 true。
bool readRegistryStringValue(const std::string& path, const std::string& key, std::string& value);
/// 写系统"程序和功能"卸载入口（DisplayName/Version/InstallLocation/UninstallString/Publisher 等）。
/// @param perMachine true 写 HKLM（全机），false 写 HKCU（当前用户）。
bool writeUninstallRegistryEntry(const std::string& appName,
                                 const std::string& displayName,
                                 const std::string& version,
                                 const std::string& installDir,
                                 const std::string& uninstallExePath,
                                 bool perMachine,
                                 const std::string& publisher = "");
/// 删除本产品的系统卸载入口（按 appName 派生的键名）。
bool deleteUninstallRegistryEntry(const std::string& appName, bool perMachine);
/// 按 DisplayName 删除系统卸载入口（用于清理改名前的旧入口）。scope 决定 hive/视图范围。
bool deleteSystemUninstallEntryByDisplayName(const std::string& displayName,
                                             UninstallEntryScope scope);

} // namespace MultiThreadedInstaller
