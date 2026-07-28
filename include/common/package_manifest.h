#pragma once

#include "common/archive_types.h"

namespace MultiThreadedInstaller {

// 本次构建的身份。其余版本资源字段（FileVersion/ProductVersion/CompanyName...）
// 由引擎从 version/publisher 在打包期派生并写入 exe PE 资源，不进 manifest。
struct PackageIdentity {
    std::string productName;  ///< 用户可见产品名；数据目录/注册表键统一用它。
    std::string appName;      ///< 主 exe 程序名（不含 .exe）；主程序定位/杀进程/立即运行用。
    std::string appId;        ///< 产品唯一 id（如 com.comp.myapp）；随 install.manifest.json 落盘。
    std::string publisher;    ///< 发布者；同时作为版本资源 CompanyName。
    std::string version;      ///< 版本号（可含 -beta 等预发布后缀）。
    std::string defaultDir;   ///< GUI 默认安装目录（支持 %ProgramFiles% 等环境变量）。
    std::string copyright;    ///< 可空；缺省时由 publisher + 构建年份派生。
};

/// 一个待安装目录（folder）的载荷描述：身份 + 在数据包中的位置 + 落点。
struct PackagePayloadFolder {
    std::string folderId;     ///< 唯一标识（取自 --input 顶层子目录名）。
    std::string folderName;   ///< 显示名。
    std::string source;       ///< 打包期源目录。
    std::string target;       ///< 安装目标（支持 %InstallDir% 与环境变量）。
    bool required = false;    ///< 是否必装（单产品单载荷下恒为全装）。
    uint64_t offset = 0;          ///< 在数据区中的字节偏移。
    uint64_t compressedSize = 0;  ///< 压缩后字节数。
    uint64_t originalSize = 0;    ///< 原始字节数。
    uint32_t checksum = 0;        ///< 校验和（落盘后校验）。
    CompressionAlgorithm algorithm = CompressionAlgorithm::LZMA2_XZ;  ///< 压缩算法。
    bool framed = false;          ///< 是否按文件分帧（支持逐文件跳过解压）。
    std::vector<FileIndexEntry> fileIndex;  ///< 文件清单（日志/校验/增量用）。
};

/// 全部载荷的汇总。
struct PackagePayloadManifest {
    uint64_t totalCompressedSize = 0;            ///< 数据区总压缩字节数。
    std::vector<PackagePayloadFolder> folders;   ///< 各 folder 载荷。
};

// 钩子结构（HookScript/PackageHooks）已收敛到 config_types.h，三层共用。

/// 构建期 → 运行期的唯一桥（精简后）：身份 + 载荷 + 钩子 + manifest 版本号。
struct PackageManifest {
    uint32_t version = Constants::VERSION;  ///< manifest 二进制版本（codec 用，拒读旧包）。
    PackageIdentity identity;               ///< 产品身份。
    PackagePayloadManifest payload;         ///< 载荷。
    PackageHooks hooks;                     ///< 钩子。
};

/// 运行期元数据 → PackageManifest（codec 编码前）。
PackageManifest PackageManifestFromExtendedMetadata(const ExtendedInstallationMetadata& metadata);
/// PackageManifest → 运行期元数据（codec 解码后，供安装流程消费）。
ExtendedInstallationMetadata PackageManifestToExtendedMetadata(const PackageManifest& manifest);

} // namespace MultiThreadedInstaller
