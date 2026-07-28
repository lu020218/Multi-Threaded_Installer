#pragma once

#include "common/archive_types.h"

#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class FolderPayloadReader;
class InstallerPathResolver;

/// 单个 folder 的解压耗时分解（计时/诊断用）。
struct FolderTiming {
    double totalSec = 0.0;       ///< 该 folder 总耗时。
    double readSec = 0.0;        ///< 读载荷耗时。
    double decompressSec = 0.0;  ///< 解压耗时。
    double writeSec = 0.0;       ///< 落盘耗时。
    std::string folderName;      ///< folder 名。
};

/// 整次并行安装的计时汇总。
struct ParallelInstallSummary {
    double payloadReadSec = 0.0;            ///< 各 folder 读载荷累计。
    double decompressSec = 0.0;             ///< 解压累计。
    double writeSec = 0.0;                  ///< 落盘累计。
    double totalSec = 0.0;                  ///< 端到端总耗时。
    std::vector<FolderTiming> folderTimings;///< 逐 folder 计时明细。
};

/// 并行安装结果。
struct ParallelInstallResult {
    bool success = false;                          ///< 是否全部成功。
    bool cancelled = false;                        ///< 是否被取消。
    bool rebootRequired = false;                   ///< 是否有锁定文件待重启替换。
    std::string installRootPath;                   ///< 解析出的安装根（含 %InstallDir% 的 folder 落点）。
    std::vector<std::string> installedRoots;       ///< 所有 folder 的目标根（卸载清理用）。
    std::vector<std::string> installedFiles;       ///< 已写入文件的绝对路径清单（写 manifest 用）。
    std::vector<std::string> pendingReplaceFiles;  ///< 被锁定、已排期重启替换的文件。
    std::vector<std::string> errors;               ///< 失败信息。
    ParallelInstallSummary timing;                 ///< 计时汇总。
};

/// 进度回调：(folderName, currentFile, progress[0..1])。
using ProgressCallback = std::function<void(const std::string&, const std::string&, float)>;
/// 日志回调（info/error 各一份）。
using LogCallback = std::function<void(const std::string&)>;
/// 取消查询回调：返回 true 表示用户已请求取消。
using CancellationCallback = std::function<bool()>;

/// 多线程并行解压并写入各 folder 的载荷，是安装解压阶段的核心。
/// @param metadata          运行期元数据（含各 folder 的 offset/size/checksum/target）。
/// @param payloadReader     载荷读取器（定位数据包内各 folder 字节）。
/// @param pathResolver      路径解析器（展开 %InstallDir%/环境变量）。
/// @param userSelectedPath  用户选定的安装根（替换 target 里的 %InstallDir%）。
/// @param folderMappings    运行期对单个 folder 目标的显式覆盖（folderId→target），通常为空。
/// @param includedFolders   参与安装的 folderId 列表。
/// @param filterFolders     true 时仅安装 includedFolders；false 时安装全部（单产品单载荷常用 false）。
/// @param threadCount       工作线程数；0 = 按 CPU 自动。
/// @param oldInstalledFingerprints 上次安装的逐文件指纹，用于"零读跳过"未变文件（方案A），可空。
/// @return 解压结果（成功/取消/重启需求 + 已装文件/根 + 计时）。
ParallelInstallResult RunParallelInstall(const PackageManifest& metadata,
                                         FolderPayloadReader& payloadReader,
                                         InstallerPathResolver& pathResolver,
                                         const std::string& userSelectedPath,
                                         const std::vector<std::pair<std::string, std::string>>& folderMappings,
                                         const std::vector<std::string>& includedFolders,
                                         bool filterFolders,
                                         int threadCount,
                                         const ProgressCallback& progressCallback,
                                         const LogCallback& infoCallback,
                                         const LogCallback& errorCallback,
                                         const CancellationCallback& cancellationCallback = {},
                                         std::shared_ptr<const InstalledFileFingerprintMap>
                                             oldInstalledFingerprints = nullptr);

} // namespace MultiThreadedInstaller
