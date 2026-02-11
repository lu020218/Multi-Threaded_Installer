#pragma once

#include "common/types.h"
#include <vector>

namespace MultiThreadedInstaller {

class MetadataGenerator {
public:
    MetadataGenerator() = default;
    ~MetadataGenerator() = default;
    

    InstallationMetadata generateMetadata(const std::vector<CompressionResult>& results,
                                        const std::vector<FolderInfo>& folderInfos);
    

    ExtendedInstallationMetadata generateExtendedMetadata(const std::vector<CompressionResult>& results,
                                                         const std::vector<FolderInfo>& folderInfos,
                                                         const PackagerConfiguration& config);
    

    std::vector<uint8_t> serializeMetadata(const InstallationMetadata& metadata);
    

    std::vector<uint8_t> serializeExtendedMetadata(const ExtendedInstallationMetadata& metadata);
    
private:

    FolderMapping createFolderMapping(const CompressionResult& result, 
                                    const FolderInfo& folderInfo, 
                                    uint64_t offset);
    

    ExtendedFolderMapping createExtendedFolderMapping(const CompressionResult& result, 
                                                      const FolderInfo& folderInfo, 
                                                      uint64_t offset,
                                                      const PackagerConfiguration& config);
    

    uint64_t calculateTotalCompressedSize(const std::vector<CompressionResult>& results);
};

} // namespace MultiThreadedInstaller
