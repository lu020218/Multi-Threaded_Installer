#include "installer/decompression_engine.h"

#include "common/installer_logger.h"
#include "installer/tar_stream_extractor.h"
#ifdef ZSTD_FOUND
#include <zstd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <thread>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

#ifdef LibLZMA_FOUND
constexpr uint64_t kMinMultiThreadedLzmaPayloadSize = 64u * 1024u * 1024u;
constexpr uint64_t kMinDecoderMemlimitThreading = 256ull * 1024ull * 1024ull;
constexpr uint64_t kMaxDecoderMemlimitThreading = 768ull * 1024ull * 1024ull;
constexpr uint64_t kDecoderMemlimitStop = 2ull * 1024ull * 1024ull * 1024ull;

uint32_t ComputeRecommendedDecoderThreads(unsigned int hardwareThreads,
                                         unsigned int schedulerConcurrencyHint) {
    const unsigned int safeHardwareThreads = (std::max)(1u, hardwareThreads);
    const unsigned int safeSchedulerConcurrency = (std::max)(1u, schedulerConcurrencyHint);
    const unsigned int perPayloadBudget =
        (std::max)(1u, safeHardwareThreads / safeSchedulerConcurrency);
    return (std::min)(4u, perPayloadBudget);
}

std::string BuildMultiThreadedDecoderDecision(const LzmaLoader& loader,
                                              const DecompressionTask& task,
                                              unsigned int threadCount,
                                              uint32_t recommendedThreads,
                                              bool* shouldUseMt) {
    std::ostringstream oss;
    const bool loaderReady = loader.isLoaded();
    const bool mtSupported = loader.supportsMultiThreadedDecompression();
    const bool hasEnoughThreads = recommendedThreads >= 2;
    const bool payloadLargeEnough = task.originalSize >= kMinMultiThreadedLzmaPayloadSize;
    *shouldUseMt = loaderReady && mtSupported && hasEnoughThreads && payloadLargeEnough;

    oss << "[DECOMP] XZ decoder decision folder=" << task.folderName
        << " original_size=" << task.originalSize
        << " compressed_size=" << task.compressedData.size()
        << " hw_threads=" << threadCount
        << " scheduler_concurrency=" << task.schedulerConcurrencyHint
        << " recommended_threads=" << recommendedThreads
        << " loader_ready=" << (loaderReady ? "true" : "false")
        << " mt_supported=" << (mtSupported ? "true" : "false")
        << " payload_large_enough=" << (payloadLargeEnough ? "true" : "false")
        << " use_mt=" << (*shouldUseMt ? "true" : "false");

    if (!*shouldUseMt) {
        oss << " reason=";
        if (!loaderReady) {
            oss << "loader_unavailable";
        } else if (!mtSupported) {
            oss << "decoder_mt_not_supported";
        } else if (!hasEnoughThreads) {
            oss << "insufficient_thread_budget";
        } else {
            oss << "payload_below_threshold";
        }
    }

    return oss.str();
}

lzma_ret InitializeLzmaDecoder(const LzmaLoader& loader,
                               lzma_stream& stream,
                               const DecompressionTask& task) {
    if (!loader.isLoaded()) {
        return LZMA_PROG_ERROR;
    }

    const unsigned int threadCount = std::thread::hardware_concurrency();
    const uint32_t recommendedThreads =
        ComputeRecommendedDecoderThreads(threadCount, task.schedulerConcurrencyHint);
    bool shouldUseMt = false;
    logInstallerInfo(BuildMultiThreadedDecoderDecision(
        loader, task, threadCount, recommendedThreads, &shouldUseMt));

    if (shouldUseMt) {
        lzma_mt options{};
        options.flags = 0;
        options.threads = recommendedThreads;
        options.timeout = 300;
        options.memlimit_threading = (std::min)(
            kMaxDecoderMemlimitThreading,
            (std::max)(kMinDecoderMemlimitThreading,
                       static_cast<uint64_t>(task.originalSize) / 2ull));
        options.memlimit_stop = kDecoderMemlimitStop;
        logInstallerInfo("[DECOMP] Using multi-threaded XZ decoder for folder=" + task.folderName +
                         " threads=" + std::to_string(options.threads) +
                         " memlimit_threading=" + std::to_string(options.memlimit_threading) +
                         " memlimit_stop=" + std::to_string(options.memlimit_stop));
        lzma_ret ret = loader.lzma_stream_decoder_mt_ptr(&stream, &options);
        if (ret == LZMA_OK) {
            return ret;
        }
        logInstallerWarning("[DECOMP] Falling back to single-threaded XZ decoder for folder=" +
                            task.folderName + " ret=" + std::to_string(ret));
    }

    logInstallerInfo("[DECOMP] Using single-threaded XZ decoder for folder=" + task.folderName);
    return loader.lzma_auto_decoder_ptr(&stream, UINT64_MAX, 0);
}
#endif

} // namespace

DecompressionEngine::DecompressionEngine() = default;
DecompressionEngine::~DecompressionEngine() = default;

bool DecompressionEngine::decompressFolder(const DecompressionTask& task, DecompressionTiming* timing) {
    TarStreamExtractor extractor(task.targetPath);
    Crc32Stream checksum;
    return decompressToStream(task, extractor, &checksum, timing);
}

bool DecompressionEngine::decompressToStream(const DecompressionTask& task,
                                             StreamSink& sink,
                                             Crc32Stream* checksum,
                                             DecompressionTiming* timing) {
    switch (task.algorithm) {
    case CompressionAlgorithm::LZMA2_XZ:
        return decompressLzma(task, sink, checksum, timing);
    case CompressionAlgorithm::ZSTD:
        return decompressZstd(task, sink, checksum, timing);
    default:
        logInstallerError("[DECOMP] Unsupported payload compression algorithm for folder: " +
                          task.folderName);
        return false;
    }
}

void DecompressionEngine::registerProgressCallback(ProgressCallback callback) {
    progressCallback_ = std::move(callback);
}

bool DecompressionEngine::decompressLzma(const DecompressionTask& task,
                                         StreamSink& sink,
                                         Crc32Stream* checksum,
                                         DecompressionTiming* timing) {
#ifdef LibLZMA_FOUND
    std::string currentFile;
    if (auto* extractor = dynamic_cast<TarStreamExtractor*>(&sink)) {
        extractor->setCurrentFileChangedCallback([&currentFile](const std::string& path) {
            currentFile = path;
        });
    }

    if (!lzmaLoader_.isLoaded()) {
        logInstallerError("[DECOMP] LZMA loader is not available for folder: " + task.folderName);
        return false;
    }
    if (task.compressedData.empty()) {
        logInstallerError("[DECOMP] No XZ/LZMA2 payload provided for folder: " + task.folderName);
        return false;
    }

    reportProgress(task.folderName, std::string(), 0.0f);

    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret = InitializeLzmaDecoder(lzmaLoader_, stream, task);
    if (ret != LZMA_OK) {
        logInstallerError("[DECOMP] Failed to initialize XZ decoder for folder '" +
                          task.folderName + "': " + std::to_string(ret));
        return false;
    }

    stream.next_in = task.compressedData.data();
    stream.avail_in = task.compressedData.size();

    std::vector<uint8_t> outBuffer(64 * 1024);
    size_t totalOut = 0;

    while (true) {
        stream.next_out = outBuffer.data();
        stream.avail_out = outBuffer.size();

        const auto decompressStart = std::chrono::steady_clock::now();
        ret = lzmaLoader_.lzma_code_ptr(&stream, LZMA_FINISH);
        const auto decompressEnd = std::chrono::steady_clock::now();
        if (timing) {
            timing->decompressNs +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(decompressEnd - decompressStart).count();
        }

        const size_t produced = outBuffer.size() - stream.avail_out;
        if (produced > 0) {
            const auto writeStart = std::chrono::steady_clock::now();
            if (!sink.write(outBuffer.data(), produced)) {
                lzmaLoader_.lzma_end_ptr(&stream);
                return false;
            }
            const auto writeEnd = std::chrono::steady_clock::now();
            if (timing) {
                timing->writeNs +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(writeEnd - writeStart).count();
            }

            if (checksum) {
                checksum->update(outBuffer.data(), produced);
            }

            totalOut += produced;
            if (task.originalSize > 0) {
                const float progress = (std::min)(
                    0.99f,
                    static_cast<float>(totalOut) / static_cast<float>(task.originalSize));
                reportProgress(task.folderName, currentFile, progress);
            }
        }

        if (ret == LZMA_STREAM_END) {
            break;
        }
        if (ret != LZMA_OK) {
            logInstallerError("[DECOMP] XZ decompression failed for folder '" + task.folderName +
                              "': " + std::to_string(ret));
            lzmaLoader_.lzma_end_ptr(&stream);
            return false;
        }
    }

    lzmaLoader_.lzma_end_ptr(&stream);
    sink.flush();

    if (checksum && checksum->finalize() != task.expectedChecksum) {
        logInstallerError("[DECOMP] Payload checksum verification failed for folder: " +
                          task.folderName);
        return false;
    }

    reportProgress(task.folderName, currentFile, 1.0f);
    return true;
#else
    (void)task;
    (void)sink;
    (void)checksum;
    (void)timing;
    logInstallerError("[DECOMP] XZ/LZMA2 support not compiled in.");
    return false;
#endif
}

bool DecompressionEngine::decompressZstd(const DecompressionTask& task,
                                         StreamSink& sink,
                                         Crc32Stream* checksum,
                                         DecompressionTiming* timing) {
#ifdef ZSTD_FOUND
    std::string currentFile;
    if (auto* extractor = dynamic_cast<TarStreamExtractor*>(&sink)) {
        extractor->setCurrentFileChangedCallback([&currentFile](const std::string& path) {
            currentFile = path;
        });
    }

    if (task.compressedData.empty()) {
        logInstallerError("[DECOMP] No ZSTD payload provided for folder: " + task.folderName);
        return false;
    }

    reportProgress(task.folderName, std::string(), 0.0f);

    ZSTD_DStream* stream = ZSTD_createDStream();
    if (!stream) {
        logInstallerError("[DECOMP] ZSTD stream allocation failed for folder: " + task.folderName);
        return false;
    }

    size_t ret = ZSTD_initDStream(stream);
    if (ZSTD_isError(ret)) {
        logInstallerError(std::string("[DECOMP] ZSTD decoder init failed for folder '") +
                          task.folderName + "': " + ZSTD_getErrorName(ret));
        ZSTD_freeDStream(stream);
        return false;
    }

    ZSTD_inBuffer input{task.compressedData.data(), task.compressedData.size(), 0};
    std::vector<uint8_t> outBuffer(64 * 1024);
    size_t totalOut = 0;

    while (input.pos < input.size) {
        ZSTD_outBuffer output{outBuffer.data(), outBuffer.size(), 0};

        const auto decompressStart = std::chrono::steady_clock::now();
        ret = ZSTD_decompressStream(stream, &output, &input);
        const auto decompressEnd = std::chrono::steady_clock::now();
        if (timing) {
            timing->decompressNs +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(decompressEnd - decompressStart).count();
        }

        if (ZSTD_isError(ret)) {
            logInstallerError(std::string("[DECOMP] ZSTD decompression failed for folder '") +
                              task.folderName + "': " + ZSTD_getErrorName(ret));
            ZSTD_freeDStream(stream);
            return false;
        }

        if (output.pos > 0) {
            const auto writeStart = std::chrono::steady_clock::now();
            if (!sink.write(outBuffer.data(), output.pos)) {
                ZSTD_freeDStream(stream);
                return false;
            }
            const auto writeEnd = std::chrono::steady_clock::now();
            if (timing) {
                timing->writeNs +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(writeEnd - writeStart).count();
            }

            if (checksum) {
                checksum->update(outBuffer.data(), output.pos);
            }

            totalOut += output.pos;
            if (task.originalSize > 0) {
                const float progress = (std::min)(
                    0.99f,
                    static_cast<float>(totalOut) / static_cast<float>(task.originalSize));
                reportProgress(task.folderName, currentFile, progress);
            }
        }
    }

    ZSTD_freeDStream(stream);
    sink.flush();

    if (checksum && checksum->finalize() != task.expectedChecksum) {
        logInstallerError("[DECOMP] Payload checksum verification failed for folder: " +
                          task.folderName);
        return false;
    }

    reportProgress(task.folderName, currentFile, 1.0f);
    return true;
#else
    (void)task;
    (void)sink;
    (void)checksum;
    (void)timing;
    logInstallerError("[DECOMP] ZSTD support not compiled in.");
    return false;
#endif
}

void DecompressionEngine::reportProgress(const std::string& folderName,
                                         const std::string& currentFile,
                                         float progress) {
    if (progressCallback_) {
        progressCallback_(folderName, currentFile, progress);
    }
}

} // namespace MultiThreadedInstaller
