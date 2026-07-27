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

// 一个 pre/post 钩子，脚本内容在打包期内嵌。
struct PackageHook {
    bool present = false;                ///< 该 hook 是否配置。
    std::string scriptName;              ///< 脚本名（仅日志用）。
    std::vector<uint8_t> content;        ///< 内嵌的脚本字节，运行期临时释放后执行。
    std::string args;                    ///< 本次构建特有的额外参数。
    HookOnFailure onFailure = HookOnFailure::ABORT;  ///< 失败处理：中止回滚 / 记日志继续。
    uint32_t timeoutSec = 300;           ///< 超时上限（秒），到点 kill 按失败处理。
    std::vector<HookAuxFile> auxFiles;   ///< 主脚本同目录的兄弟文件（递归内嵌），随主脚本一同释放。
    bool keep = false;                   ///< 执行后是否把脚本+兄弟文件保留到 keepDir。
    std::string keepDir;                 ///< 保留目标目录（支持 %INSTALL_DIR%/%VERSION%/系统环境变量）。
};

/// 安装前/后两个固定钩子点；每个钩子点可挂多个脚本，按声明顺序依次执行。
struct PackageHooks {
    std::vector<PackageHook> preInstall;   ///< 解压前依次执行。
    std::vector<PackageHook> postInstall;  ///< finalize 后依次执行。
};

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
