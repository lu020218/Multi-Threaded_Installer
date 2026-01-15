#pragma once

#include "common/types.h"
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class InstallerGenerator {
public:
    InstallerGenerator() = default;
    ~InstallerGenerator() = default;
    
    // 生成安装程序
    bool generateInstaller(const std::string& outputPath,
                          const std::vector<uint8_t>& metadata,
                          const std::vector<std::vector<uint8_t>>& compressedData);
    
    // 生成外部数据包
    bool generateDataPackage(const std::string& outputPath,
                            const std::vector<uint8_t>& metadata,
                            const std::vector<std::vector<uint8_t>>& compressedData);
    
    // 嵌入安装程序模板
    bool embedInstallerTemplate(const std::string& templatePath);
    
private:
    // 数据定位结构，用于安装程序查找嵌入的数据
    struct DataLocator {
        uint32_t magic;           // 魔数标识
        uint64_t metadataOffset;  // 元数据偏移量
        uint64_t metadataSize;    // 元数据大小
        uint64_t dataOffset;      // 压缩数据偏移量
        uint64_t dataSize;        // 压缩数据总大小
        
        DataLocator() : magic(0), metadataOffset(0), metadataSize(0), 
                       dataOffset(0), dataSize(0) {}
    };
    
    std::string installerTemplatePath;
    
    // 创建自解压可执行文件
    bool createSelfExtractingExecutable(const std::string& outputPath,
                                      const std::vector<uint8_t>& metadata,
                                      const std::vector<std::vector<uint8_t>>& compressedData);
    
    // 获取默认安装程序模板
    std::vector<uint8_t> getDefaultInstallerTemplate();
    
    // 从文件加载安装程序模板
    std::vector<uint8_t> loadInstallerTemplate(const std::string& templatePath);
    
    // 创建占位符模板（用于测试）
    std::vector<uint8_t> createPlaceholderTemplate();
    
    // 设置可执行权限
    bool setExecutablePermissions(const std::string& filePath);
    
    // 将数据附加到可执行文件
    bool appendDataToExecutable(const std::string& executablePath,
                               const std::vector<uint8_t>& data);
};

} // namespace MultiThreadedInstaller
