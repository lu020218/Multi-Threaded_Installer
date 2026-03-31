#pragma once

#include "common/archive_types.h"
#include "installer/decompression_engine.h"
#include "installer/folder_payload_reader.h"

#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// Describes one folder payload installation unit. The mapping points to a
// single compressed payload and the executor installs it to one resolved target.
struct FolderInstallRequest {
    std::string folderName;
    ExtendedFolderMapping mapping;
    std::string resolvedTargetPath;
    unsigned int schedulerConcurrencyHint = 1;
    std::function<bool()> cancellationCallback;
    std::function<void(const std::string&)> infoCallback;
    std::function<void(const std::string&)> errorCallback;
};

struct FolderInstallResult {
    bool success = false;
    bool cancelled = false;
    std::string folderName;
    std::string targetPath;
    std::vector<std::string> errors;
    double readSec = 0.0;
    double decompressSec = 0.0;
    double writeSec = 0.0;
    double totalSec = 0.0;
};

// Executes exactly one folder payload installation by reading the payload and
// streaming it through the standard decompressor into TarStreamExtractor.
class FolderInstallExecutor {
public:
    FolderInstallExecutor(FolderPayloadReader& payloadReader,
                          DecompressionEngine& decompressionEngine);

    FolderInstallResult execute(const FolderInstallRequest& request);

private:
    FolderPayloadReader& payloadReader_;
    DecompressionEngine& decompressionEngine_;
};

} // namespace MultiThreadedInstaller
