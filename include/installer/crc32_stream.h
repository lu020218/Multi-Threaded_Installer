#pragma once

#include <cstdint>
#include <cstddef>

namespace MultiThreadedInstaller {

class Crc32Stream {
public:
    Crc32Stream() : crc_(0xFFFFFFFFu) {}
    
    void reset() { crc_ = 0xFFFFFFFFu; }
    
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
    
    uint32_t finalize() const {
        return ~crc_;
    }
    
private:
    uint32_t crc_;
};

} // namespace MultiThreadedInstaller
