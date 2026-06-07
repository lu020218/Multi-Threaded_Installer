#pragma once

#include "common/archive_types.h"
#include "common/lzma_loader.h"
#include "installer/payload/crc32_stream.h"
#include "installer/payload/stream_sink.h"

namespace MultiThreadedInstaller {

// Standard folder-payload decompressor. It only handles whole-folder XZ/LZMA2
// or ZSTD streams and writes them to a StreamSink; block-based install formats
// are intentionally no longer supported.
class DecompressionEngine {
public:
    struct DecompressionTiming {
        long long decompressNs = 0;
        long long writeNs = 0;
    };

    struct DecompressionOutcome {
        bool rebootRequired = false;
        std::vector<std::string> pendingReplaceFiles;
        std::vector<std::string> installedFiles;
        std::vector<std::string> skippedFiles;
    };

    DecompressionEngine();
    ~DecompressionEngine();

    bool decompressFolder(const DecompressionTask& task,
                          DecompressionTiming* timing = nullptr,
                          DecompressionOutcome* outcome = nullptr);

    bool decompressToStream(const DecompressionTask& task,
                            StreamSink& sink,
                            Crc32Stream* checksum = nullptr,
                            DecompressionTiming* timing = nullptr,
                            DecompressionOutcome* outcome = nullptr);

    void registerProgressCallback(ProgressCallback callback);

private:
    ProgressCallback progressCallback_;
    LzmaLoader lzmaLoader_;

    bool decompressLzma(const DecompressionTask& task,
                        StreamSink& sink,
                        Crc32Stream* checksum,
                        DecompressionTiming* timing);
    bool decompressZstd(const DecompressionTask& task,
                        StreamSink& sink,
                        Crc32Stream* checksum,
                        DecompressionTiming* timing);
    void reportProgress(const std::string& folderName,
                        const std::string& currentFile,
                        float progress);
};

} // namespace MultiThreadedInstaller
