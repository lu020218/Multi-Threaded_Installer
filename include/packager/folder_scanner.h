#pragma once

#include "common/types.h"
#include <vector>
#include <string>

namespace MultiThreadedInstaller {

class FolderScanner {
public:
    FolderScanner() = default;
    ~FolderScanner() = default;
    
    // 扫描输入目录，返回所有子文件夹信息
    std::vector<FolderInfo> scanInputDirectory(const std::string& inputPath);
    
    // 验证文件夹结构的有效性
    bool validateFolderStructure(const std::vector<FolderInfo>& folders);
    
private:
    // 递归扫描单个文件夹
    void scanSingleFolder(const std::string& folderPath, FolderInfo& folderInfo);
    
    // 计算文件夹总大小
    size_t calculateFolderSize(const std::vector<std::string>& files);
    
    // 检查路径是否为目录
    bool isDirectory(const std::string& path);
    
    // 检查文件是否存在且可读
    bool isFileReadable(const std::string& filePath);
};

} // namespace MultiThreadedInstaller