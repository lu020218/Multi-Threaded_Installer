#pragma once

#include "common/package_manifest.h"

namespace MultiThreadedInstaller {

/// 打包期 manifest 构建器：把压缩结果 + 目录信息 + 配置组装成 PackageManifest
/// （identity + payload + hooks），随后由 codec 序列化并内嵌进安装器 exe。
class PackageManifestBuilder {
public:
    /// 构建 manifest。
    /// @param results        各 folder 的压缩结果（offset/size/checksum/fileIndex 等）。
    /// @param folderInfos    各 folder 的源信息（id/target）。
    /// @param config         打包配置（app/package/hooks/layout）。
    /// @param configDirectory 用于解析并内嵌 hook 脚本（path 相对 --config 目录）。
    PackageManifest build(const std::vector<CompressionResult>& results,
                          const std::vector<FolderInfo>& folderInfos,
                          const PackagerConfiguration& config,
                          const std::string& configDirectory) const;
};

} // namespace MultiThreadedInstaller
