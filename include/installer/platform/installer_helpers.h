#pragma once

#include "common/archive_types.h"
#include "installer/platform/path_resolver.h"
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#else
#include <unistd.h>
#endif

namespace MultiThreadedInstaller {

// 安装/卸载通用辅助函数集合：长路径、文件预分配、PE 内嵌数据定位、管理员权限、磁盘/系统
// 版本预检、进程管理、快捷方式等。多为 best-effort，失败返回 false/空由调用方处理。

// ── 文件/路径 ──────────────────────────────────────────────────────────
/// 转为 Win32 长路径形式（加 \\?\ 前缀，突破 MAX_PATH 限制）。
std::filesystem::path toLongPath(const std::filesystem::path& path);
/// 确保文件存在且至少 size 字节；超过 sparseThresholdBytes 时用稀疏文件以省空间。
bool ensureFileWithSize(const std::filesystem::path& path, uint64_t size,
                        uint64_t sparseThresholdBytes = 4 * 1024 * 1024);
/// 以写模式打开文件（必要时创建父目录）。
bool openFileForWrite(const std::filesystem::path& path, std::fstream& stream);

// ── 快捷方式/自启（与 shortcut_startup_utils.h 同名声明，历史保留）──────────
std::filesystem::path findPrimaryExecutable(const std::filesystem::path& installRoot,
                                            const std::string& appName);  ///< 找主可执行文件。
bool setAutoStartup(const std::string& appName, const std::filesystem::path& exePath);  ///< 设开机自启。
bool removeAutoStartup(const std::string& appName);                                     ///< 移除开机自启。
/// 建桌面快捷方式。@param iconPath 可选，手动指定图标（如安装目录下的 app.ico），留空用 exe 自带图标。
/// 注意：默认参数仅在 shortcut_startup_utils.h 声明，避免与本历史重复声明冲突。
bool createDesktopShortcut(const std::string& appName,
                           const std::filesystem::path& exePath,
                           const std::filesystem::path& iconPath);
bool deleteDesktopShortcut(const std::string& appName);                                 ///< 删桌面快捷方式。
bool createStartMenuShortcut(const std::string& appName,
                             const std::filesystem::path& exePath,
                             const std::string& uninstallDisplayName);  ///< 建开始菜单快捷方式。
bool deleteStartMenuShortcut(const std::string& appName);              ///< 删开始菜单快捷方式。

// ── 字符串/比较 ────────────────────────────────────────────────────────
/// 归一化路径用于比较（统一分隔符、去尾斜杠、转小写）。
std::string normalizePathForCompare(const std::string& path);
/// 判断一条消息文本是否表示"用户取消"。
bool isCancellationText(const std::string& message);

// ── 安装器自身（PE 内嵌数据）──────────────────────────────────────────
/// 从文件指定 offset 读 size 字节到 out。
bool readFileBytesAt(std::ifstream& file, uint64_t offset, void* out, size_t size);
/// 计算 PE 文件的逻辑末尾（内嵌数据追加在其后）。
uint64_t resolveLogicalPeEnd(std::ifstream& file, uint64_t fileSize);

/// 内嵌数据定位记录：元数据/数据区在安装器 exe 中的偏移与大小。
struct EmbeddedDataLocatorRecord {
    uint32_t magic;          ///< 魔数（校验定位记录有效性）。
    uint64_t metadataOffset; ///< 元数据偏移。
    uint64_t metadataSize;   ///< 元数据大小。
    uint64_t dataOffset;     ///< 数据区偏移。
    uint64_t dataSize;       ///< 数据区大小。

    EmbeddedDataLocatorRecord()
        : magic(0), metadataOffset(0), metadataSize(0), dataOffset(0), dataSize(0) {}
};

/// 在安装器 exe 末尾查找内嵌数据定位记录。
bool findEmbeddedDataLocator(std::ifstream& file,
                             uint64_t fileSize,
                             uint64_t& trailerEnd,
                             EmbeddedDataLocatorRecord& locator);

/// 取当前可执行文件完整路径（UTF-8）。
std::string getCurrentExecutablePath();
/// 若 basePath 末段不是 expectedLeaf，则补上该末段。
std::string appendPathLeafIfMissing(const std::string& basePath, const std::string& expectedLeaf);
/// 由安装器 exe 路径推断同目录的 install.manifest.json 路径。
std::string getLocalManifestPath(const std::string& exePath);
/// 释放/拷贝出卸载器存根到目标路径。
bool createUninstallStub(const std::string& sourcePath, const std::string& targetPath);

// ── 权限/提权 ──────────────────────────────────────────────────────────
/// 当前进程是否以管理员权限运行。
bool isRunningAsAdmin();
/// 本次安装是否需要管理员权限（重构后写死为 EngineDefaults::kRequireAdmin）。
bool requiresAdminForInstall(const std::string& installPath,
                             const ExtendedInstallationMetadata& metadata,
                             InstallerPathResolver& resolver);
/// 以管理员权限重启自身（ShellExecute runas）。
bool relaunchSelfAsAdmin();
/// 以管理员权限重启自身并追加参数。
bool relaunchSelfAsAdminWithArguments(const std::vector<std::wstring>& extraArgs);

// ── 预检 ───────────────────────────────────────────────────────────────
/// 取指定路径所在卷的可用空间（字节）。
uint64_t getAvailableDiskSpaceBytes(const std::string& path);
/// 检查磁盘空间是否够安装。@param availableBytes 出参：实际可用。
bool checkDiskSpaceForInstall(const std::string& path, uint64_t requiredBytes,
                              uint64_t& availableBytes);
/// 检查当前系统是否满足最低 Windows 版本要求。出参返回当前版本三段。
bool checkMinimumWindowsVersion(uint16_t minMajor, uint16_t minMinor, uint32_t minBuild,
                                uint16_t& currentMajor, uint16_t& currentMinor, uint32_t& currentBuild);

// ── 进程管理 ───────────────────────────────────────────────────────────
bool isProcessRunningByName(const std::string& exeName);   ///< 按名判断进程是否在运行。
bool terminateProcessByName(const std::string& exeName);   ///< 按名结束进程。
std::string normalizeProcessName(const std::string& name); ///< 归一化进程名（补 .exe、转小写等）。
/// 由产品名 + 额外进程名构造去重的待结束进程清单。
std::vector<std::string> buildKillProcessList(const std::string& appName,
                                              const std::vector<std::string>& extraProcesses);
std::vector<std::string> getRunningProcessesByName(const std::vector<std::string>& exeNames);  ///< 过滤出在运行的。
bool terminateProcessesByName(const std::vector<std::string>& exeNames);  ///< 批量结束。

} // namespace MultiThreadedInstaller
