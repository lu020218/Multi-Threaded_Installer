#pragma once

#include "common/types.h"
#include <vector>

namespace MultiThreadedInstaller {

class MetadataGenerator {
public:
    MetadataGenerator() = default;
    ~MetadataGenerator() = default;
    
    // 从压缩结果生成安装元数据
    InstallationMetadata generateMetadata(const std::vector<CompressionResult>& results,
                                        const std::vector<FolderInfo>& folderInfos);
    
    // 从压缩结果生成扩展安装元数据（支持配置）
    ExtendedInstallationMetadata generateExtendedMetadata(const std::vector<CompressionResult>& results,
                                                         const std::vector<FolderInfo>& folderInfos,
                                                         const PackagerConfiguration& config);
    
    // 序列化元数据为二进制格式
    std::vector<uint8_t> serializeMetadata(const InstallationMetadata& metadata);
    
    // 序列化扩展元数据为二进制格式（向后兼容）
    std::vector<uint8_t> serializeExtendedMetadata(const ExtendedInstallationMetadata& metadata);
    
private:
    // 创建文件夹映射
    FolderMapping createFolderMapping(const CompressionResult& result, 
                                    const FolderInfo& folderInfo, 
                                    uint64_t offset);
    
    // 创建扩展文件夹映射
    ExtendedFolderMapping createExtendedFolderMapping(const CompressionResult& result, 
                                                      const FolderInfo& folderInfo, 
                                                      uint64_t offset,
                                                      const PackagerConfiguration& config);
    
    // 计算总压缩大小
    uint64_t calculateTotalCompressedSize(const std::vector<CompressionResult>& results);
};

} // namespace MultiThreadedInstaller