#pragma once

#include "common/archive_types.h"
#include "installer/payload/decompression_engine.h"
#include "installer/payload/folder_payload_reader.h"

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
    // Previously installed fingerprints for the zero-read skip path. May be null.
    std::shared_ptr<const InstalledFileFingerprintMap> oldInstalledFingerprints;
    std::function<bool()> cancellationCallback;
    std::function<void(const std::string&)> infoCallback;
    std::function<void(const std::string&)> errorCallback;
};

struct FolderInstallResult {
    bool success = false;
    bool cancelled = false;
    bool rebootRequired = false;
    std::string folderName;
    std::string targetPath;
    std::vector<std::string> pendingReplaceFiles;
    std::vector<std::string> installedFiles;
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
