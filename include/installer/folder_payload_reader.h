#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// Reads a single folder payload from the installer data area. The payload is a
// complete standard XZ/LZMA2 or ZSTD stream; no block-level format remains.
class FolderPayloadReader {
public:
    explicit FolderPayloadReader(std::string dataPackagePath = {});

    std::vector<uint8_t> readPayload(uint64_t offset,
                                     uint64_t size,
                                     std::string* errorMessage = nullptr) const;

private:
    std::string dataPackagePath_;
};

} // namespace MultiThreadedInstaller
