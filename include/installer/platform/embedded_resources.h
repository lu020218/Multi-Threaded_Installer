#pragma once

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/**
 *
 *
 */
class EmbeddedResourceManager {
public:
    EmbeddedResourceManager();
    ~EmbeddedResourceManager();

    // 读取嵌入的二进制资源（原生 PE 资源，类型 "BINARY"）。
    // GUI 资源（RES_ZIP）由此读入内存后直接交给 DuiLib，不再释放到临时磁盘。
    std::vector<uint8_t> getEmbeddedResource(const std::string& name);
};

std::vector<uint8_t> LoadEmbeddedBinaryResource(const std::string& name);
bool ExtractEmbeddedBinaryResourceToFile(const std::string& name, const std::string& outputPath);

/// 向磁盘上的 PE 文件注入一个 BINARY 类型资源（保留其已有图标/版本/清单等资源）。
/// 安装时把 RES_ZIP 注入释放出的 uninstall.exe，使卸载器自包含、不依赖任何散落资源文件。
bool InjectBinaryResourceIntoFile(const std::string& filePath,
                                  const std::string& name,
                                  const std::vector<uint8_t>& data);

} // namespace MultiThreadedInstaller
