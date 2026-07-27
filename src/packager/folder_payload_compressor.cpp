#include "packager/folder_payload_compressor.h"

#include "common/content_hash.h"
#include "common/utf8_utils.h"

#include <algorithm>
#include <array>
#include <atomic>
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

// 解析 XZ 多线程分块大小。显式配置(>0)优先；否则自动:
//   目标约 kTargetParallelBlocks 块(与安装器解码线程上限 min(4,…) 对齐)——块尽量大以减少
//   字典重置、提升压缩比;同时夹在 [64MiB, 256MiB]:下限保证块≥字典(比率)、避免碎块,
//   上限避免块过大撑爆解码 memlimit_threading(512–768MiB)反而退化解码并行。
//   小载荷(< 下限×目标)自然落到单块,压缩比最佳(此时本就不会启用多线程解压)。
uint64_t ResolveXzBlockSize(uint64_t configuredBytes, uint64_t tarSize) {
    if (configuredBytes > 0) {
        return configuredBytes;
    }
    if (tarSize == 0) {
        return 0;  // 交给 liblzma 自行决定
    }
    constexpr uint64_t kTargetParallelBlocks = 4;
    constexpr uint64_t kAutoBlockFloor = 64ull * 1024 * 1024;
    constexpr uint64_t kAutoBlockCap = 256ull * 1024 * 1024;
    uint64_t blockSize = (tarSize + kTargetParallelBlocks - 1) / kTargetParallelBlocks;
    if (blockSize < kAutoBlockFloor) {
        blockSize = kAutoBlockFloor;
    }
    if (blockSize > kAutoBlockCap) {
        blockSize = kAutoBlockCap;
    }
    return blockSize;
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

// Compresses a single buffer into a self-contained XZ stream (one frame).
std::vector<uint8_t> CompressBufferToXz(const uint8_t* data, size_t size, int level) {
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_easy_encoder(&stream, static_cast<uint32_t>(level), LZMA_CHECK_CRC32) != LZMA_OK) {
        return {};
    }
    stream.next_in = data;
    stream.avail_in = size;
    std::vector<uint8_t> output;
    if (!AppendXzStreamChunk(output, stream, LZMA_FINISH)) {
        lzma_end(&stream);
        return {};
    }
    lzma_end(&stream);
    return output;
}
#endif

#ifdef ZSTD_FOUND
// Compresses a single buffer into a self-contained ZSTD frame.
std::vector<uint8_t> CompressBufferToZstd(const uint8_t* data, size_t size, int level) {
    const size_t bound = ZSTD_compressBound(size);
    if (bound == 0) {
        return {};
    }
    std::vector<uint8_t> output(bound);
    const size_t produced = ZSTD_compress(output.data(), bound, data, size, level);
    if (ZSTD_isError(produced)) {
        return {};
    }
    output.resize(produced);
    return output;
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
    if (perFileFrames_) {
        return compressFolderFramed(folder);
    }
    if (currentAlgorithm == CompressionAlgorithm::ZSTD) {
        return compressWithZstd(folder);
    }
    return compressWithXzLzma2(folder);
}

// ── 分帧策略常量（引擎写死，无外部配置）─────────────────────────────────────
// 小文件独立成帧会失去跨文件压缩字典（实测真实包体积 +13.9%），因此小于阈值的文件按扫描
// 顺序聚合进同一"批帧"压缩，帧内以 entry.offset 记录各文件在解压后缓冲中的偏移；
// 大文件仍独立成帧（升级跳过粒度最优）。批帧目标大小兼顾压缩比与"帧内任一文件变化则
// 整帧重解压"的放大代价。
constexpr uint64_t kSmallFileFrameThreshold = 4ull * 1024ull * 1024ull;  // < 4MB 走聚合
constexpr uint64_t kBatchFrameTargetBytes = 32ull * 1024ull * 1024ull;   // 批帧目标 ~32MB（重解压一帧 ~0.3s 级）
constexpr size_t kFrameCompressWorkersCap = 16;                          // 帧级并行压缩上限

CompressionResult FolderPayloadCompressor::compressFolderFramed(const FolderInfo& folder) const {
    CompressionResult result;
    result.algorithm = currentAlgorithm;
    result.framed = true;

    const size_t sourcePrefixLength = folder.sourcePath.size();
    auto toRelative = [&](const std::string& filePath) {
        std::string relativePath = filePath;
        if (relativePath.compare(0, sourcePrefixLength, folder.sourcePath) == 0) {
            relativePath = relativePath.substr(sourcePrefixLength);
            if (!relativePath.empty() && (relativePath[0] == '/' || relativePath[0] == '\\')) {
                relativePath = relativePath.substr(1);
            }
        }
        return relativePath;
    };

    // 1) 预扫尺寸，按策略切分帧任务：大文件独帧；小文件按序聚合到 ~kBatchFrameTargetBytes。
    struct MemberPlan {
        std::string filePath;
        std::string relativePath;
        uint64_t size = 0;
    };
    struct FrameJob {
        std::vector<MemberPlan> members;
        uint64_t totalSize = 0;
        // 压缩产物（并行阶段填写）
        std::vector<uint8_t> frame;
        std::vector<uint64_t> memberHashes;
        std::vector<uint64_t> memberOffsets;
        bool failed = false;
        std::string error;
    };

    std::vector<FrameJob> jobs;
    FrameJob batch;
    auto closeBatch = [&]() {
        if (!batch.members.empty()) {
            jobs.push_back(std::move(batch));
            batch = FrameJob{};
        }
    };
    for (const auto& filePath : folder.files) {
        std::error_code sizeError;
        const uint64_t fileSize64 = std::filesystem::file_size(PathFromUtf8(filePath), sizeError);
        if (sizeError) {
            std::cerr << "Failed to get file size: " << filePath << " (" << sizeError.message()
                      << ")" << std::endl;
            return CompressionResult{};
        }
        if (fileSize64 > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            std::cerr << "File too large for payload entry format: " << filePath << std::endl;
            return CompressionResult{};
        }
        MemberPlan member{filePath, toRelative(filePath), fileSize64};
        if (fileSize64 >= kSmallFileFrameThreshold) {
            closeBatch();
            FrameJob solo;
            solo.totalSize = fileSize64;
            solo.members.push_back(std::move(member));
            jobs.push_back(std::move(solo));
            continue;
        }
        batch.totalSize += fileSize64;
        batch.members.push_back(std::move(member));
        if (batch.totalSize >= kBatchFrameTargetBytes) {
            closeBatch();
        }
    }
    closeBatch();

    // 2) 帧级并行压缩：每个任务读入成员文件到连续缓冲（记录帧内偏移与逐成员哈希）后整帧压缩。
    const size_t workerCount = std::min<size_t>(
        {jobs.empty() ? size_t{1} : jobs.size(),
         static_cast<size_t>(ResolveCompressionThreads(threadCount)),
         kFrameCompressWorkersCap});
    std::atomic<size_t> nextJob{0};
    std::atomic<bool> anyFailed{false};
    auto compressWorker = [&]() {
        for (;;) {
            const size_t index = nextJob.fetch_add(1);
            if (index >= jobs.size() || anyFailed.load()) {
                return;
            }
            FrameJob& job = jobs[index];
            std::vector<uint8_t> content;
            content.reserve(static_cast<size_t>(job.totalSize));
            job.memberOffsets.reserve(job.members.size());
            job.memberHashes.reserve(job.members.size());
            for (const auto& member : job.members) {
                job.memberOffsets.push_back(static_cast<uint64_t>(content.size()));
                const size_t before = content.size();
                if (member.size > 0) {
                    std::ifstream file(PathFromUtf8(member.filePath), std::ios::binary);
                    if (!file) {
                        job.failed = true;
                        job.error = "Failed to open file: " + member.filePath;
                        anyFailed.store(true);
                        return;
                    }
                    content.resize(before + static_cast<size_t>(member.size));
                    file.read(reinterpret_cast<char*>(content.data() + before),
                              static_cast<std::streamsize>(member.size));
                    if (!file) {
                        job.failed = true;
                        job.error = "Failed to read file: " + member.filePath;
                        anyFailed.store(true);
                        return;
                    }
                }
                job.memberHashes.push_back(
                    ComputeContentHash64(content.data() + before, content.size() - before));
            }

            if (currentAlgorithm == CompressionAlgorithm::ZSTD) {
#ifdef ZSTD_FOUND
                job.frame = CompressBufferToZstd(content.data(), content.size(), compressionLevel);
#else
                job.failed = true;
                job.error = "ZSTD support not compiled in";
                anyFailed.store(true);
                return;
#endif
            } else {
#ifdef LibLZMA_FOUND
                job.frame = CompressBufferToXz(content.data(), content.size(), compressionLevel);
#else
                job.frame = content; // stub: store uncompressed when no codec
#endif
            }
            if (job.frame.empty() && !content.empty()) {
                job.failed = true;
                job.error = "Failed to compress frame (first member: " +
                            job.members.front().filePath + ")";
                anyFailed.store(true);
                return;
            }
        }
    };
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        workers.emplace_back(compressWorker);
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (const auto& job : jobs) {
        if (job.failed) {
            std::cerr << job.error << std::endl;
            return CompressionResult{};
        }
    }

    // 3) 按任务顺序串行拼装载荷与 fileIndex（frameOffset 依赖前序帧大小，必须顺序累计）。
    std::vector<uint8_t> payload;
    std::vector<FileIndexEntry> fileIndex;
    fileIndex.reserve(folder.files.size());
    uint64_t totalOriginal = 0;
    size_t batchFrameCount = 0;
    for (auto& job : jobs) {
        const uint64_t frameOffset = static_cast<uint64_t>(payload.size());
        const uint64_t frameCompressedSize = static_cast<uint64_t>(job.frame.size());
        if (job.members.size() > 1) {
            ++batchFrameCount;
        }
        for (size_t m = 0; m < job.members.size(); ++m) {
            FileIndexEntry entry;
            entry.relativePath = job.members[m].relativePath;
            entry.offset = job.memberOffsets[m];  // 帧内偏移（解压后缓冲中的位置）
            entry.size = job.members[m].size;
            entry.contentHash = job.memberHashes[m];
            entry.frameOffset = frameOffset;
            entry.frameCompressedSize = frameCompressedSize;
            fileIndex.push_back(std::move(entry));
            totalOriginal += job.members[m].size;
        }
        payload.insert(payload.end(), job.frame.begin(), job.frame.end());
        job.frame.clear();
        job.frame.shrink_to_fit();
    }

    std::cout << "[Packager][Payload] folder=" << folder.sourcePath
              << " algorithm=" << (currentAlgorithm == CompressionAlgorithm::ZSTD ? "ZSTD" : "XZ/LZMA2")
              << " mode=per-file-frames files=" << fileIndex.size()
              << " frames=" << jobs.size()
              << " batchFrames=" << batchFrameCount
              << " workers=" << workerCount
              << " originalSize=" << totalOriginal
              << " compressedSize=" << payload.size()
              << std::endl;

    result.originalSize = totalOriginal;
    result.compressedData = std::move(payload);
    result.compressedSize = result.compressedData.size();
    // Per-file contentHash provides integrity for framed payloads, so there is
    // no single whole-folder stream to checksum.
    result.checksum = 0;
    result.fileIndex = std::move(fileIndex);
    return result;
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
    // 解析分块大小：显式配置优先；否则自动按 tar 大小对齐“解码并行块数”(kTargetParallelBlocks)，
    // 让块尽量大(更少 dict 重置、压缩比更高)同时仍够喂满多线程解压。小载荷退化为单块(最佳比率)。
    const uint64_t effectiveBlockSize = ResolveXzBlockSize(blockSizeBytes_, result.originalSize);
    std::cout << "[Packager][Payload] folder=" << folder.sourcePath
              << " algorithm=XZ/LZMA2"
              << " level=" << compressionLevel
              << " threads=" << resolvedThreads
              << " blockSize=" << effectiveBlockSize
              << " (" << (blockSizeBytes_ > 0 ? "configured" : "auto") << ")"
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
        options.block_size = effectiveBlockSize;  // 0 时由 liblzma 自行决定
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
        // The file content is already in tarData; fingerprint it in place so
        // the installer can later skip rewriting unchanged files.
        entry.contentHash =
            fileSize > 0 ? ComputeContentHash64(tarData.data() + payloadOffset, fileSize)
                         : ComputeContentHash64(nullptr, 0);
        fileIndex.push_back(std::move(entry));
    }

    return tarData;
}

} // namespace MultiThreadedInstaller
