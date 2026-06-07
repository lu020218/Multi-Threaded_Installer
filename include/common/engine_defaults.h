#pragma once

#include <cstdint>
#include <string>

namespace MultiThreadedInstaller {

// ---------------------------------------------------------------------------
// 引擎写死的默认值与约定（需求 §5/§7）。
//
// 这些是稳定机制的取值，不在 YAML 暴露。如某项将来确需按构建变化，再单独提回 YAML。
// 跨版本兼容逻辑不在这里——一律走引擎内的迁移函数表（见 installer/migration）。
// ---------------------------------------------------------------------------
namespace EngineDefaults {

// 权限：要求管理员。
constexpr bool kRequireAdmin = true;

// 最低支持的 Windows 版本（10.0.19041）。
constexpr uint16_t kMinWindowsMajor = 10;
constexpr uint16_t kMinWindowsMinor = 0;
constexpr uint32_t kMinWindowsBuild = 19041;

// 大文件（稀疏）阈值。
constexpr uint64_t kSparseFileThresholdBytes = 4ull * 1024 * 1024;

// 默认界面语言。
inline const char* kDefaultLanguage = "zh_CN";

// 安装行为默认值（用户可在 GUI 改）。
constexpr bool kDefaultAutoStartup = false;
constexpr bool kDefaultDesktopShortcut = true;

// 互斥量：避免并发安装/卸载。
constexpr bool kUseMutex = true;

// 注册表根（HKLM\Software\<ProductName>）。
inline const char* kRegistryRootHive = "HKEY_LOCAL_MACHINE";
inline const char* kRegistrySoftwarePrefix = "Software\\";

// install-state.json 落在 %ProgramData%\<ProductName>\ 下。
inline const char* kInstallStateFileName = "install-state.json";

// ── 由产品名派生的稳定值 ────────────────────────────────────────────────

inline std::string MutexName(const std::string& productName) {
    return "Global\\" + productName + "_Install";
}

inline std::string RegistryPath(const std::string& productName) {
    return std::string(kRegistryRootHive) + "\\" + kRegistrySoftwarePrefix + productName;
}

// 数据目录统一使用产品名（需求 §5 数据目录）。
inline std::string ProgramDataDir(const std::string& productName) {
    return std::string("%ProgramData%\\") + productName;
}

inline std::string InstallStateFilePath(const std::string& productName) {
    return ProgramDataDir(productName) + "\\" + kInstallStateFileName;
}

} // namespace EngineDefaults
} // namespace MultiThreadedInstaller
