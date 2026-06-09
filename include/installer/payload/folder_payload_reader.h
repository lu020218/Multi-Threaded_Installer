#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/// 从安装器数据区读取单个 folder 的载荷字节。载荷是完整的标准 XZ/LZMA2 或 ZSTD 流，
/// 不再有块级私有格式。dataPackagePath 为空时从当前 exe 自身的内嵌数据区读取。
class FolderPayloadReader {
public:
    /// @param dataPackagePath 外部数据包路径；为空表示读当前可执行文件内嵌的数据区。
    explicit FolderPayloadReader(std::string dataPackagePath = {});

    /// 按 [offset, offset+size) 读取一个 folder 的压缩载荷字节。
    /// @param errorMessage 失败原因（可选）。
    /// @return 读出的字节；失败返回空并填充 errorMessage。
    std::vector<uint8_t> readPayload(uint64_t offset,
                                     uint64_t size,
                                     std::string* errorMessage = nullptr) const;

private:
    /// 单个载荷大小上限（4 GiB），防御异常 offset/size。
    static constexpr uint64_t kMaxPayloadSizeBytes = 4ull * 1024ull * 1024ull * 1024ull;

    std::string dataPackagePath_;  ///< 数据包路径；空 = 读自身内嵌数据区。
};

} // namespace MultiThreadedInstaller
