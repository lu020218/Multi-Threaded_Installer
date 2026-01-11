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
    
    // 序列化元数据为二进制格式
    std::vector<uint8_t> serializeMetadata(const InstallationMetadata& metadata);
    
private:
    // 创建文件夹映射
    FolderMapping createFolderMapping(const CompressionResult& result, 
                                    const FolderInfo& folderInfo, 
                                    uint64_t offset);
    
    // 计算总压缩大小
    uint64_t calculateTotalCompressedSize(const std::vector<CompressionResult>& results);
};

} // namespace MultiThreadedInstaller