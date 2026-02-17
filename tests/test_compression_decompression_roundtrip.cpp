#include "packager/compression_module.h"
#include "installer/decompression_engine.h"
#include "installer/stream_sink.h"
#include "installer/crc32_stream.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using MultiThreadedInstaller::CompressionAlgorithm;
using MultiThreadedInstaller::CompressionModule;
using MultiThreadedInstaller::Crc32Stream;
using MultiThreadedInstaller::DecompressionEngine;
using MultiThreadedInstaller::DecompressionTask;
using MultiThreadedInstaller::FolderInfo;
using MultiThreadedInstaller::StreamSink;

void AssertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct MemorySink final : StreamSink {
    std::vector<uint8_t> data;

    bool write(const uint8_t* input, size_t size) override {
        if (size == 0) {
            return true;
        }
        if (input == nullptr) {
            return false;
        }
        data.insert(data.end(), input, input + size);
        return true;
    }

    void flush() override {}
};

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    AssertTrue(!ec, "Failed to create parent directory for " + path.string());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    AssertTrue(static_cast<bool>(out), "Failed to open file for writing: " + path.string());
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    AssertTrue(static_cast<bool>(out), "Failed to write file: " + path.string());
}

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    AssertTrue(static_cast<bool>(in), "Failed to open file for reading: " + path.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string RelativePathLikePackager(const std::string& sourceRoot, const std::string& filePath) {
    std::string relativePath = filePath;
    if (relativePath.find(sourceRoot) == 0) {
        relativePath = relativePath.substr(sourceRoot.length());
        if (!relativePath.empty() && (relativePath[0] == '/' || relativePath[0] == '\\')) {
            relativePath = relativePath.substr(1);
        }
    }
    return relativePath;
}

std::map<std::string, std::vector<uint8_t>> ParseTarPayload(const std::vector<uint8_t>& payload) {
    std::map<std::string, std::vector<uint8_t>> files;
    size_t pos = 0;

    while (pos < payload.size()) {
        AssertTrue(pos + sizeof(uint32_t) * 2 <= payload.size(), "Invalid payload header boundaries");

        uint32_t pathLength = 0;
        uint32_t fileSize = 0;
        std::memcpy(&pathLength, payload.data() + pos, sizeof(uint32_t));
        pos += sizeof(uint32_t);
        std::memcpy(&fileSize, payload.data() + pos, sizeof(uint32_t));
        pos += sizeof(uint32_t);

        AssertTrue(pos + pathLength <= payload.size(), "Invalid payload path boundaries");
        std::string path(reinterpret_cast<const char*>(payload.data() + pos), pathLength);
        pos += pathLength;

        AssertTrue(pos + fileSize <= payload.size(), "Invalid payload file boundaries");
        std::vector<uint8_t> content(payload.begin() + static_cast<std::ptrdiff_t>(pos),
                                     payload.begin() + static_cast<std::ptrdiff_t>(pos + fileSize));
        pos += fileSize;

        files.emplace(std::move(path), std::move(content));
    }

    return files;
}

void RunRoundtripCase(CompressionAlgorithm algorithm, int level) {
    const std::string algorithmName = (algorithm == CompressionAlgorithm::ZSTD) ? "zstd" : "lzma";
    const auto tempRoot = std::filesystem::temp_directory_path() /
                          ("mti_roundtrip_" + algorithmName);
    const auto inputRoot = tempRoot / "input";

    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
    std::filesystem::create_directories(inputRoot, ec);
    AssertTrue(!ec, "Failed to prepare temporary directory");

    WriteFile(inputRoot / "bin" / "app.txt", "hello-from-main-binary\nline2\n");
    WriteFile(inputRoot / "config" / "settings.ini", "[general]\nname=demo\n");
    WriteFile(inputRoot / "data" / "payload.bin", std::string(4096, 'A'));

    FolderInfo folder;
    folder.sourcePath = inputRoot.string();

    std::map<std::string, std::vector<uint8_t>> expectedFiles;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(inputRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string pathText = entry.path().string();
        folder.files.push_back(pathText);
        expectedFiles.emplace(RelativePathLikePackager(folder.sourcePath, pathText),
                              ReadFileBytes(entry.path()));
    }

    CompressionModule compressor;
    AssertTrue(compressor.setCompressionAlgorithm(algorithm), "Failed to set compression algorithm");
    AssertTrue(compressor.setCompressionLevel(level), "Failed to set compression level");

    const auto compressed = compressor.compressFolder(folder);
    AssertTrue(!compressed.compressedData.empty(), "Compression output is empty");
    AssertTrue(compressed.algorithm == algorithm, "Compression result algorithm mismatch");

    DecompressionTask task;
    task.compressedData = compressed.compressedData;
    task.algorithm = compressed.algorithm;
    task.expectedChecksum = compressed.checksum;
    task.originalSize = compressed.originalSize;
    task.targetPath = (tempRoot / "unused_target").string();

    MemorySink sink;
    Crc32Stream checksum;
    DecompressionEngine engine;
    AssertTrue(engine.decompressToStream(task, sink, &checksum, nullptr),
               "Decompression failed for algorithm: " + algorithmName);

    const auto unpacked = ParseTarPayload(sink.data);
    AssertTrue(unpacked.size() == expectedFiles.size(), "Unpacked file count mismatch");
    AssertTrue(unpacked == expectedFiles, "Unpacked payload content mismatch");

    std::filesystem::remove_all(tempRoot, ec);
}

} // namespace

int main() {
    try {
#ifdef LibLZMA_FOUND
        RunRoundtripCase(CompressionAlgorithm::LZMA_HIGH, 6);
#else
        std::cout << "skip lzma roundtrip: LibLZMA not available" << std::endl;
#endif

#ifdef ZSTD_FOUND
        RunRoundtripCase(CompressionAlgorithm::ZSTD, 3);
#else
        std::cout << "skip zstd roundtrip: ZSTD not available" << std::endl;
#endif

        std::cout << "compression/decompression roundtrip test passed" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "compression/decompression roundtrip test failed: " << ex.what() << std::endl;
        return 1;
    }
}
