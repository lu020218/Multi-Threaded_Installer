#pragma once

#include "common/types.h"

namespace MultiThreadedInstaller {

class MetadataParser {
public:
    MetadataParser() = default;
    ~MetadataParser() = default;
    
    // 解析嵌入的元数据
    InstallationMetadata parseEmbeddedMetadata();
    
    // 解析嵌入的扩展元数据
    ExtendedInstallationMetadata parseExtendedEmbeddedMetadata();
    
    // 验证元数据的有效性
    bool validateMetadata(const InstallationMetadata& metadata);
    
    // 反序列化二进制元数据 (public for testing)
    InstallationMetadata deserializeMetadata(const std::vector<uint8_t>& data);
    
    // 反序列化扩展二进制元数据 (public for testing)
    ExtendedInstallationMetadata deserializeExtendedMetadata(const std::vector<uint8_t>& data);
    
    // 读取压缩数据
    std::vector<uint8_t> readCompressedData(uint64_t offset, uint64_t size);
    
private:
    // 数据定位结构，与InstallerGenerator中的结构相同
    struct DataLocator {
        uint32_t magic;           // 魔数标识
        uint64_t metadataOffset;  // 元数据偏移量
        uint64_t metadataSize;    // 元数据大小
        uint64_t dataOffset;      // 压缩数据偏移量
        uint64_t dataSize;        // 压缩数据总大小
        
        DataLocator() : magic(0), metadataOffset(0), metadataSize(0), 
                       dataOffset(0), dataSize(0) {}
    };
    
    // 从当前可执行文件读取嵌入的数据
    std::vector<uint8_t> readEmbeddedData();
    
    // 验证魔数和版本
    bool validateHeader(const BinaryMetadata& header);
    
    // 获取当前可执行文件路径
    std::string getCurrentExecutablePath();
};

} // namespace MultiThreadedInstaller