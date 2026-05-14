#pragma once

#include "common/archive_types.h"

#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class FolderPayloadReader;
class InstallerPathResolver;

struct FolderTiming {
    double totalSec = 0.0;
    double readSec = 0.0;
    double decompressSec = 0.0;
    double writeSec = 0.0;
    std::string folderName;
};

struct ParallelInstallSummary {
    double payloadReadSec = 0.0;
    double decompressSec = 0.0;
    double writeSec = 0.0;
    double totalSec = 0.0;
    std::vector<FolderTiming> folderTimings;
};

struct ParallelInstallResult {
    bool success = false;
    bool cancelled = false;
    bool rebootRequired = false;
    std::string installRootPath;
    std::vector<std::string> installedRoots;
    std::vector<std::string> installedFiles;
    std::vector<std::string> pendingReplaceFiles;
    std::vector<std::string> errors;
    ParallelInstallSummary timing;
};

using ProgressCallback = std::function<void(const std::string&, const std::string&, float)>;
using LogCallback = std::function<void(const std::string&)>;
using CancellationCallback = std::function<bool()>;

ParallelInstallResult RunParallelInstall(const ExtendedInstallationMetadata& metadata,
                                         FolderPayloadReader& payloadReader,
                                         InstallerPathResolver& pathResolver,
                                         const std::string& userSelectedPath,
                                         const std::vector<std::pair<std::string, std::string>>& folderMappings,
                                         const std::vector<std::string>& includedFolders,
                                         bool filterFolders,
                                         int threadCount,
                                         const ProgressCallback& progressCallback,
                                         const LogCallback& infoCallback,
                                         const LogCallback& errorCallback,
                                         const CancellationCallback& cancellationCallback = {});

} // namespace MultiThreadedInstaller
