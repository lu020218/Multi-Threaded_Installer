#include "packager/folder_payload_compressor.h"

#include "common/utf8_utils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

#ifdef LibLZMA_FOUND
#include <lzma.h>
#endif
#ifdef ZSTD_FOUND
#include <zstd.h>
#endif

namespace MultiThreadedInstaller {

namespace {

int GetDefaultCompressionLevel(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::ZSTD:
            return Constants::DEFAULT_ZSTD_LEVEL;
        case CompressionAlgorithm::LZMA2_XZ:
        default:
            return Constants::DEFAULT_LZMA_LEVEL;
    }
}

unsigned int ResolveCompressionThreads(int configuredThreadCount) {
    if (configuredThreadCount > 0) {
        return static_cast<unsigned int>(configuredThreadCount);
    }
    unsigned int hwThreads = std::thread::hardware_concurrency();
    return hwThreads == 0 ? 1u : hwThreads;
}

const std::array<uint32_t, 256>& GetCrc32Table() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < values.size(); ++i) {
            uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
            }
            values[i] = crc;
        }
        return values;
    }();
    return table;
}

#ifdef LibLZMA_FOUND
bool SupportsMultiThreadedXzEncoding() {
#if LZMA_VERSION >= 500200
    return true;
#else
    return false;
#endif
}

bool AppendXzStreamChunk(std::vector<uint8_t>& output,
                         lzma_stream& stream,
                         lzma_action action) {
    std::array<uint8_t, 64 * 1024> buffer{};
    while (true) {
        stream.next_out = buffer.data();
        stream.avail_out = buffer.size();

        const lzma_ret ret = lzma_code(&stream, action);
        if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
            return false;
        }

        const size_t produced = buffer.size() - stream.avail_out;
        output.insert(output.end(), buffer.data(), buffer.data() + produced);

        if (ret == LZMA_STREAM_END || stream.avail_out != 0) {
            return true;
        }
    }
}
#endif

} // namespace

FolderPayloadCompressor::FolderPayloadCompressor()
    : currentAlgorithm(CompressionAlgorithm::LZMA2_XZ),
      compressionLevel(Constants::DEFAULT_LZMA_LEVEL),
      threadCount(0) {}

bool FolderPayloadCompressor::setCompressionAlgorithm(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::LZMA2_XZ:
        case CompressionAlgorithm::ZSTD:
            currentAlgorithm = algorithm;
            compressionLevel = GetDefaultCompressionLevel(algorithm);
            return true;
        default:
            return false;
    }
}

bool FolderPayloadCompressor::setCompressionLevel(int level) {
    if (currentAlgorithm == CompressionAlgorithm::LZMA2_XZ) {
        if (level < 0 || level > 9) {
            return false;
        }
    } else if (currentAlgorithm == CompressionAlgorithm::ZSTD) {
        if (level < 1 || level > 22) {
            return false;
        }
    }

    compressionLevel = level;
    return true;
}

bool FolderPayloadCompressor::setThreadCount(int count) {
    if (count == 0 || count < -1) {
        return false;
    }
    threadCount = count;
    return true;
}

CompressionResult FolderPayloadCompressor::compressFolder(const FolderInfo& folder) const {
    if (currentAlgorithm == CompressionAlgorithm::ZSTD) {
        return compressWithZstd(folder);
    }
    return compressWithXzLzma2(folder);
}

CompressionResult FolderPayloadCompressor::compressWithXzLzma2(const FolderInfo& folder) const {
    CompressionResult result;
    result.algorithm = CompressionAlgorithm::LZMA2_XZ;

    std::vector<FileIndexEntry> fileIndex;
    std::vector<uint8_t> tarData = createTarData(folder, fileIndex);
    if (tarData.empty() && !folder.files.empty()) {
        std::cerr << "Failed to build folder payload stream for: " << folder.sourcePath << std::endl;
        return result;
    }

    result.originalSize = tarData.size();
    result.fileIndex = std::move(fileIndex);
    result.checksum = calculateChecksum(tarData);

#ifdef LibLZMA_FOUND
    const unsigned int resolvedThreads = ResolveCompressionThreads(threadCount);
    std::cout << "[Packager][Payload] folder=" << folder.sourcePath
              << " algorithm=XZ/LZMA2"
              << " level=" << compressionLevel
              << " threads=" << resolvedThreads
              << " originalSize=" << result.originalSize
              << std::endl;

    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret = LZMA_OK;

#if LZMA_VERSION >= 500200
    if (SupportsMultiThreadedXzEncoding()) {
        lzma_mt options{};
        options.threads = resolvedThreads;
        options.preset = static_cast<uint32_t>(compressionLevel);
        options.check = LZMA_CHECK_SHA256;
        ret = lzma_stream_encoder_mt(&stream, &options);
    } else {
        ret = lzma_easy_encoder(&stream, compressionLevel, LZMA_CHECK_SHA256);
    }
#else
    ret = lzma_easy_encoder(&stream, compressionLevel, LZMA_CHECK_SHA256);
#endif

    if (ret != LZMA_OK) {
        std::cerr << "Failed to initialize XZ/LZMA2 encoder: " << ret << std::endl;
        return CompressionResult{};
    }

    stream.next_in = tarData.data();
    stream.avail_in = tarData.size();

    if (!AppendXzStreamChunk(result.compressedData, stream, LZMA_FINISH)) {
        std::cerr << "XZ/LZMA2 compression failed for folder: " << folder.sourcePath << std::endl;
        lzma_end(&stream);
        return CompressionResult{};
    }

    lzma_end(&stream);
    result.compressedSize = result.compressedData.size();
    return result;
#else
    result.compressedData = tarData;
    result.compressedSize = tarData.size();
    std::cout << "Using stub XZ/LZMA2 implementation (no actual compression)" << std::endl;
    return result;
#endif
}

CompressionResult FolderPayloadCompressor::compressWithZstd(const FolderInfo& folder) const {
    CompressionResult result;
    result.algorithm = CompressionAlgorithm::ZSTD;

    std::vector<FileIndexEntry> fileIndex;
    std::vector<uint8_t> tarData = createTarData(folder, fileIndex);
    if (tarData.empty() && !folder.files.empty()) {
        std::cerr << "Failed to build folder payload stream for: " << folder.sourcePath << std::endl;
        return result;
    }

    result.originalSize = tarData.size();
    result.fileIndex = std::move(fileIndex);
    result.checksum = calculateChecksum(tarData);

#ifdef ZSTD_FOUND
    std::cout << "[Packager][Payload] folder=" << folder.sourcePath
              << " algorithm=ZSTD"
              << " level=" << compressionLevel
              << " threads=1"
              << " originalSize=" << result.originalSize
              << std::endl;

    const size_t bound = ZSTD_compressBound(tarData.size());
    if (bound == 0) {
        std::cerr << "ZSTD failed to calculate compression bound" << std::endl;
        return CompressionResult{};
    }

    result.compressedData.resize(bound);
    ZSTD_CCtx* context = ZSTD_createCCtx();
    if (context == nullptr) {
        std::cerr << "ZSTD context allocation failed" << std::endl;
        return CompressionResult{};
    }

    const size_t compressedSize = ZSTD_compressCCtx(
        context,
        result.compressedData.data(),
        result.compressedData.size(),
        tarData.data(),
        tarData.size(),
        compressionLevel);
    ZSTD_freeCCtx(context);

    if (ZSTD_isError(compressedSize)) {
        std::cerr << "ZSTD compression failed: " << ZSTD_getErrorName(compressedSize) << std::endl;
        return CompressionResult{};
    }

    result.compressedData.resize(compressedSize);
    result.compressedSize = compressedSize;
    return result;
#else
    std::cerr << "ZSTD support not compiled in" << std::endl;
    return CompressionResult{};
#endif
}

uint32_t FolderPayloadCompressor::calculateChecksum(const std::vector<uint8_t>& data) const {
    uint32_t crc = 0xFFFFFFFFu;
    const auto& table = GetCrc32Table();

    for (uint8_t byte : data) {
        crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFFu];
    }

    return ~crc;
}

std::vector<uint8_t> FolderPayloadCompressor::createTarData(
    const FolderInfo& folder,
    std::vector<FileIndexEntry>& fileIndex) const {
    std::vector<uint8_t> tarData;
    fileIndex.clear();
    fileIndex.reserve(folder.files.size());
    tarData.reserve(folder.totalSize + folder.files.size() * (sizeof(uint32_t) * 2 + 64));

    const size_t sourcePrefixLength = folder.sourcePath.size();
    for (const auto& filePath : folder.files) {
        std::error_code sizeError;
        uint64_t fileSize64 = std::filesystem::file_size(PathFromUtf8(filePath), sizeError);
        if (sizeError) {
            std::cerr << "Failed to get file size: " << filePath << " (" << sizeError.message()
                      << ")" << std::endl;
            return {};
        }
        if (fileSize64 > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            std::cerr << "File too large for payload entry format: " << filePath << std::endl;
            return {};
        }

        const uint32_t fileSize = static_cast<uint32_t>(fileSize64);
        std::string relativePath = filePath;
        if (relativePath.compare(0, sourcePrefixLength, folder.sourcePath) == 0) {
            relativePath = relativePath.substr(sourcePrefixLength);
            if (!relativePath.empty() && (relativePath[0] == '/' || relativePath[0] == '\\')) {
                relativePath = relativePath.substr(1);
            }
        }

        const uint32_t pathLength = static_cast<uint32_t>(relativePath.length());
        const size_t entryStart = tarData.size();
        const size_t payloadOffset =
            entryStart + sizeof(uint32_t) + sizeof(uint32_t) + pathLength;
        tarData.resize(payloadOffset + fileSize);

        std::memcpy(tarData.data() + entryStart, &pathLength, sizeof(pathLength));
        std::memcpy(
            tarData.data() + entryStart + sizeof(uint32_t), &fileSize, sizeof(fileSize));
        if (pathLength > 0) {
            std::memcpy(
                tarData.data() + entryStart + sizeof(uint32_t) + sizeof(uint32_t),
                relativePath.data(),
                pathLength);
        }

        if (fileSize > 0) {
            std::ifstream file(PathFromUtf8(filePath), std::ios::binary);
            if (!file) {
                std::cerr << "Failed to open file: " << filePath << std::endl;
                return {};
            }
            file.read(
                reinterpret_cast<char*>(tarData.data() + payloadOffset),
                static_cast<std::streamsize>(fileSize));
            if (!file) {
                std::cerr << "Failed to read file: " << filePath << std::endl;
                return {};
            }
        }

        FileIndexEntry entry;
        entry.relativePath = relativePath;
        entry.offset = static_cast<uint64_t>(payloadOffset);
        entry.size = static_cast<uint64_t>(fileSize);
        fileIndex.push_back(std::move(entry));
    }

    return tarData;
}

} // namespace MultiThreadedInstaller
