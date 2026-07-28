#pragma once

#include "common/archive_types.h"
#include "installer/payload/decompression_engine.h"
#include "installer/payload/folder_payload_reader.h"

#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/// 描述一个 folder 载荷的安装单元：mapping 指向单个压缩载荷，执行器把它装到 resolvedTargetPath。
struct FolderInstallRequest {
    std::string folderName;            ///< folder 名（日志/进度用）。
    PackagePayloadFolder mapping;     ///< 载荷映射（offset/size/checksum/算法/fileIndex）。
    std::string resolvedTargetPath;    ///< 已解析的安装目标（%InstallDir%/环境变量已展开）。
    unsigned int schedulerConcurrencyHint = 1;  ///< 并发提示（影响解压线程预算）。
    /// 上次安装的逐文件指纹，用于"零读跳过"未变文件（方案A），可空。
    std::shared_ptr<const InstalledFileFingerprintMap> oldInstalledFingerprints;
    std::function<bool()> cancellationCallback;             ///< 取消查询。
    std::function<void(const std::string&)> infoCallback;   ///< 信息日志回调。
    std::function<void(const std::string&)> errorCallback;  ///< 错误日志回调。
};

/// 单个 folder 安装结果（含计时分解）。
struct FolderInstallResult {
    bool success = false;                          ///< 是否成功。
    bool cancelled = false;                        ///< 是否被取消。
    bool rebootRequired = false;                   ///< 是否有锁定文件待重启替换。
    std::string folderName;                        ///< folder 名。
    std::string targetPath;                        ///< 实际目标路径。
    std::vector<std::string> pendingReplaceFiles;  ///< 待重启替换的文件。
    std::vector<std::string> installedFiles;       ///< 已写入文件。
    std::vector<std::string> errors;               ///< 失败信息。
    double readSec = 0.0;        ///< 读载荷耗时。
    double decompressSec = 0.0;  ///< 解压耗时。
    double writeSec = 0.0;       ///< 落盘耗时。
    double totalSec = 0.0;       ///< 总耗时。
};

/// 执行恰好一个 folder 载荷的安装：读载荷 → 经标准解压器流式喂给 TarStreamExtractor 落地。
class FolderInstallExecutor {
public:
    FolderInstallExecutor(FolderPayloadReader& payloadReader,
                          DecompressionEngine& decompressionEngine);

    /// 执行一次 folder 安装并返回结果（成功/取消/重启需求 + 已装文件 + 计时）。
    FolderInstallResult execute(const FolderInstallRequest& request);

private:
    FolderPayloadReader& payloadReader_;          ///< 载荷读取器（外部持有）。
    DecompressionEngine& decompressionEngine_;    ///< 解压引擎（外部持有）。
};

} // namespace MultiThreadedInstaller
