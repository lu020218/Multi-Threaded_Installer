#pragma once

#include "common/archive_types.h"
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class InstallerGenerator {
public:
    InstallerGenerator() = default;
    ~InstallerGenerator() = default;
    

    bool generateInstaller(const std::string& outputPath,
                          const std::vector<uint8_t>& metadata,
                          const std::vector<CompressionResult>& compressionResults);

    const std::string& getLastError() const { return lastError_; }
    

    bool generateDataPackage(const std::string& outputPath,
                            const std::vector<uint8_t>& metadata,
                            const std::vector<CompressionResult>& compressionResults);
    

    bool embedInstallerTemplate(const std::string& templatePath);

    void setResourceDirectory(const std::string& resourceDirectory);

    std::string findDefaultInstallerTemplatePath() const;
    
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
    
    std::string installerTemplatePath;
    std::string resourceDirectoryPath_;
    std::string lastError_;
    

    bool createSelfExtractingExecutable(const std::string& outputPath,
                                      const std::vector<uint8_t>& metadata,
                                      const std::vector<CompressionResult>& compressionResults);

    bool setExecutablePermissions(const std::string& filePath);

    bool copyRuntimeDependencies(const std::string& installerPath);
};

} // namespace MultiThreadedInstaller
