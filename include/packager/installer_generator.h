#pragma once

#include "common/archive_types.h"
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/// 安装器生成器：把元数据 + 各 folder 压缩载荷追加到安装器模板 exe 之后，并写入定位记录，
/// 产出自解压安装器（installer.exe 模板 + 内嵌数据）。也可单独产出外部数据包。
class InstallerGenerator {
public:
    InstallerGenerator() = default;
    ~InstallerGenerator() = default;

    /// 生成自解压安装器：拷贝模板 → 追加元数据/载荷 → 写定位记录。
    bool generateInstaller(const std::string& outputPath,
                          const std::vector<uint8_t>& metadata,
                          const std::vector<CompressionResult>& compressionResults);

    const std::string& getLastError() const { return lastError_; }  ///< 最近一次错误信息。

    /// 仅生成外部数据包（不含模板 exe），用于分发数据与安装器分离的场景。
    bool generateDataPackage(const std::string& outputPath,
                            const std::vector<uint8_t>& metadata,
                            const std::vector<CompressionResult>& compressionResults);

    bool embedInstallerTemplate(const std::string& templatePath);  ///< 指定/嵌入安装器模板。
    void setResourceDirectory(const std::string& resourceDirectory);  ///< 设置资源目录。
    std::string findDefaultInstallerTemplatePath() const;             ///< 查找默认模板路径。

private:
    /// 数据定位记录（写在文件末尾，运行期据此找到元数据/数据区）。
    struct DataLocator {
        uint32_t magic;
        uint64_t metadataOffset;
        uint64_t metadataSize;
        uint64_t dataOffset;
        uint64_t dataSize;

        DataLocator() : magic(0), metadataOffset(0), metadataSize(0),
                       dataOffset(0), dataSize(0) {}
    };

    std::string installerTemplatePath;    ///< 安装器模板路径。
    std::string resourceDirectoryPath_;   ///< 资源目录。
    std::string lastError_;               ///< 最近错误。

    /// 核心实现：拼出自解压 exe（模板 + 元数据 + 载荷 + 定位记录）。
    bool createSelfExtractingExecutable(const std::string& outputPath,
                                      const std::vector<uint8_t>& metadata,
                                      const std::vector<CompressionResult>& compressionResults);
    bool setExecutablePermissions(const std::string& filePath);   ///< 设置可执行权限（非 Windows）。
    bool copyRuntimeDependencies(const std::string& installerPath);  ///< 拷贝运行时依赖（如有）。
};

} // namespace MultiThreadedInstaller
