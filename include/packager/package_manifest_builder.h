#pragma once

#include "common/package_manifest.h"

namespace MultiThreadedInstaller {

class PackageManifestBuilder {
public:
    PackageManifest build(const std::vector<CompressionResult>& results,
                          const std::vector<FolderInfo>& folderInfos,
                          const PackagerConfiguration& config) const;
};

} // namespace MultiThreadedInstaller
