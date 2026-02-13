#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <filesystem>

namespace MultiThreadedInstaller {

class InstallerGenerator {
public:
    InstallerGenerator() = default;
    ~InstallerGenerator() = default;
    

    bool generateInstaller(const std::string& outputPath,
                          const std::vector<uint8_t>& metadata,
                          const std::vector<std::vector<uint8_t>>& compressedData);

    const std::string& getLastError() const { return lastError_; }
    

    bool generateDataPackage(const std::string& outputPath,
                            const std::vector<uint8_t>& metadata,
                            const std::vector<std::vector<uint8_t>>& compressedData);
    

    bool embedInstallerTemplate(const std::string& templatePath);


    std::string findDefaultInstallerTemplatePath() const;


    void setTemplateResourceDir(const std::filesystem::path& resourceDir) {
        templateResourceDirOverride = resourceDir;
    }
    
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
    std::filesystem::path templateResourceDirOverride;
    std::string lastError_;
    

    bool createSelfExtractingExecutable(const std::string& outputPath,
                                      const std::vector<uint8_t>& metadata,
                                      const std::vector<std::vector<uint8_t>>& compressedData);
    

    std::vector<uint8_t> getDefaultInstallerTemplate();
    

    std::vector<uint8_t> loadInstallerTemplate(const std::string& templatePath);
    

    std::vector<uint8_t> createPlaceholderTemplate();
    

    bool setExecutablePermissions(const std::string& filePath);
    

    bool appendDataToExecutable(const std::string& executablePath,
                               const std::vector<uint8_t>& data);
    

    bool copyRuntimeDependencies(const std::string& installerPath);

    std::filesystem::path resolveTemplateDirectory() const;
    bool appendEmbeddedResources(std::vector<uint8_t>& installerTemplate,
                                 const std::filesystem::path& resourceDir);
    bool hasEmbeddedResourceTable(const std::vector<uint8_t>& installerTemplate) const;
};

} // namespace MultiThreadedInstaller
