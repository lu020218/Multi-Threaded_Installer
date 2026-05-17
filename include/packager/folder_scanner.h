#pragma once

#include "common/config_types.h"
#include <vector>
#include <string>

namespace MultiThreadedInstaller {

class FolderScanner {
public:
    FolderScanner() = default;
    ~FolderScanner() = default;
    

    std::vector<FolderInfo> scanInputDirectory(const std::string& inputPath);

    std::vector<FolderInfo> scanConfiguredPayloads(const std::string& inputPath,
                                                   const std::vector<PayloadConfig>& payloads);
    

    bool validateFolderStructure(const std::vector<FolderInfo>& folders);
    
private:

    void scanSingleFolder(const std::string& folderPath, FolderInfo& folderInfo);
    

    size_t calculateFolderSize(const std::vector<std::string>& files);
    

    bool isDirectory(const std::string& path);
    

    bool isFileReadable(const std::string& filePath);
};

} // namespace MultiThreadedInstaller
