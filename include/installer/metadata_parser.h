#pragma once

#include "common/types.h"

namespace MultiThreadedInstaller {

class MetadataParser {
public:
    MetadataParser() = default;
    ~MetadataParser() = default;
    

    InstallationMetadata parseEmbeddedMetadata();
    

    ExtendedInstallationMetadata parseExtendedEmbeddedMetadata();
    

    bool validateMetadata(const InstallationMetadata& metadata);
    

    InstallationMetadata deserializeMetadata(const std::vector<uint8_t>& data);
    

    ExtendedInstallationMetadata deserializeExtendedMetadata(const std::vector<uint8_t>& data);
    

    std::vector<uint8_t> readCompressedData(uint64_t offset, uint64_t size);
    

    void setDataPackagePath(const std::string& dataPackagePath) { dataPackagePath_ = dataPackagePath; }
    
private:

    struct DataLocator {
        uint32_t magic;
        uint64_t metadataOffset;
        uint64_t metadataSize;
        uint64_t dataOffset;
        uint64_t dataSize;
        
        DataLocator() : magic(0), metadataOffset(0), metadataSize(0), 
                       dataOffset(0), dataSize(0) {}
    };
    

    std::vector<uint8_t> readEmbeddedData();
    

    std::vector<uint8_t> readExternalMetadata();
    

    std::vector<uint8_t> readExternalCompressedData(uint64_t offset, uint64_t size);
    

    bool validateHeader(const BinaryMetadata& header);
    

    std::string getCurrentExecutablePath();
    
    std::string dataPackagePath_;
};

} // namespace MultiThreadedInstaller
