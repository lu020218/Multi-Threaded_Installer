#pragma once

#include "common/archive_types.h"
#include <fstream>

namespace MultiThreadedInstaller {

class MetadataParser {
public:
    MetadataParser() = default;
    ~MetadataParser() = default;
    
    ExtendedInstallationMetadata parseExtendedEmbeddedMetadata();
    

    bool validateMetadata(const ExtendedInstallationMetadata& metadata);
    
    ExtendedInstallationMetadata deserializeExtendedMetadata(const std::vector<uint8_t>& data);

    void setDataPackagePath(const std::string& dataPackagePath) { dataPackagePath_ = dataPackagePath; }
    const std::string& getDataPackagePath() const { return dataPackagePath_; }
    
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
    bool readEmbeddedLocator(std::ifstream& file,
                             uint64_t fileSize,
                             uint64_t& logicalEnd,
                             DataLocator& locator);
    
    std::string dataPackagePath_;
};

} // namespace MultiThreadedInstaller
