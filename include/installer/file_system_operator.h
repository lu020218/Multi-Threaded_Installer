#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace MultiThreadedInstaller {

class FileSystemOperator {
public:
    FileSystemOperator() = default;
    ~FileSystemOperator() = default;
    
    // 递归创建目录
    bool createDirectoryRecursive(const std::string& path);
    
    // 写入文件
    bool writeFile(const std::string& filePath, const std::vector<uint8_t>& data);
    
    // 验证文件完整性
    bool verifyFileIntegrity(const std::string& filePath, uint32_t expectedChecksum);
    
    // 处理文件冲突（直接覆盖）
    bool handleFileConflict(const std::string& filePath);
    
    // 检查文件是否存在
    bool fileExists(const std::string& filePath);
    
    // 获取文件大小
    size_t getFileSize(const std::string& filePath);
    
    // 检查目录是否存在
    bool directoryExists(const std::string& dirPath);
    
    // 获取文件的校验和
    uint32_t getFileChecksum(const std::string& filePath);
    
private:
    // 创建单级目录
    bool createSingleDirectory(const std::string& path);
    
    // 获取父目录路径
    std::string getParentDirectory(const std::string& path);
    
    // 规范化路径
    std::string normalizePath(const std::string& path);
    
    // 计算数据的校验和
    uint32_t calculateChecksum(const std::vector<uint8_t>& data);
};

} // namespace MultiThreadedInstaller