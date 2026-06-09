#pragma once

#include <cstdint>
#include <cstddef>

namespace MultiThreadedInstaller {

/// 流式 CRC32（IEEE 802.3 多项式 0xEDB88320），可分块喂入，用于解压后校验载荷完整性。
class Crc32Stream {
public:
    Crc32Stream() : crc_(0xFFFFFFFFu) {}

    /// 重置为初始状态，复用同一对象计算下一段。
    void reset() { crc_ = 0xFFFFFFFFu; }

    /// 累计一段数据。
    void update(const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            crc_ ^= data[i];
            for (int j = 0; j < 8; ++j) {
                if (crc_ & 1u) {
                    crc_ = (crc_ >> 1) ^ 0xEDB88320u;
                } else {
                    crc_ >>= 1;
                }
            }
        }
    }
    
    /// 取最终 CRC32 值（不改变内部状态）。
    uint32_t finalize() const {
        return ~crc_;
    }

private:
    uint32_t crc_;  ///< 运行中的 CRC 中间值。
};

} // namespace MultiThreadedInstaller
