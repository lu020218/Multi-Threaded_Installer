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

} // namespace MultiThreadedInstaller
