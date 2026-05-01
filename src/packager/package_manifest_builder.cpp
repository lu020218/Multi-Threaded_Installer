#include "packager/package_manifest_builder.h"

#include "packager/metadata_generator.h"

namespace MultiThreadedInstaller {

PackageManifest PackageManifestBuilder::build(const std::vector<CompressionResult>& results,
                                              const std::vector<FolderInfo>& folderInfos,
                                              const PackagerConfiguration& config) const {
    MetadataGenerator generator;
    ExtendedInstallationMetadata metadata =
        generator.generateExtendedMetadata(results, folderInfos, config);
    return PackageManifestFromExtendedMetadata(metadata);
}

} // namespace MultiThreadedInstaller
