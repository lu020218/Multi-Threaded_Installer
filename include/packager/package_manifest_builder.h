#pragma once

#include "common/package_manifest.h"

namespace MultiThreadedInstaller {

class PackageManifestBuilder {
public:
    // configDirectory 用于解析并内嵌 hook 脚本（path 相对 --config 目录）。
    PackageManifest build(const std::vector<CompressionResult>& results,
                          const std::vector<FolderInfo>& folderInfos,
                          const PackagerConfiguration& config,
                          const std::string& configDirectory) const;
};

} // namespace MultiThreadedInstaller
