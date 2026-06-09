#pragma once

#include "common/archive_types.h"
#include <fstream>

namespace MultiThreadedInstaller {

/// 从安装器自身（内嵌）或外部数据包读取并反序列化运行期元数据 ExtendedInstallationMetadata。
class MetadataParser {
public:
    MetadataParser() = default;
    ~MetadataParser() = default;

    /// 解析内嵌元数据。deferFileIndex=true 时跳过逐文件 fileIndex（仅填文件夹/标量元数据），
    /// 供 GUI 启动关键路径加速；安装 worker 会以 deferFileIndex=false 重解析以取得 fileIndex。
    ExtendedInstallationMetadata parseExtendedEmbeddedMetadata(bool deferFileIndex = false);

    /// 校验元数据（委托 package_manifest_validator）。无效返回 false 并记日志。
    bool validateMetadata(const ExtendedInstallationMetadata& metadata);

    /// 把一段元数据字节反序列化为运行期元数据（含 manifest 解码 + 校验）。
    ExtendedInstallationMetadata deserializeExtendedMetadata(const std::vector<uint8_t>& data,
                                                             bool deferFileIndex = false);

    /// 设置外部数据包路径（为空表示读当前 exe 内嵌数据）。
    void setDataPackagePath(const std::string& dataPackagePath) { dataPackagePath_ = dataPackagePath; }
    const std::string& getDataPackagePath() const { return dataPackagePath_; }

private:
    /// 内嵌数据定位记录：元数据/数据区在文件中的偏移与大小。
    struct DataLocator {
        uint32_t magic;
        uint64_t metadataOffset;
        uint64_t metadataSize;
        uint64_t dataOffset;
        uint64_t dataSize;

        DataLocator() : magic(0), metadataOffset(0), metadataSize(0),
                       dataOffset(0), dataSize(0) {}
    };

    std::vector<uint8_t> readEmbeddedData();      ///< 从当前 exe 末尾定位并读出内嵌元数据字节。
    std::vector<uint8_t> readExternalMetadata();  ///< 从外部数据包读出元数据字节。
    /// 在 exe 文件中定位内嵌数据 locator，并校验偏移/大小落在合法范围内。
    bool readEmbeddedLocator(std::ifstream& file,
                             uint64_t fileSize,
                             uint64_t& logicalEnd,
                             DataLocator& locator);

    std::string dataPackagePath_;  ///< 外部数据包路径；空 = 读自身内嵌数据。
};

} // namespace MultiThreadedInstaller
