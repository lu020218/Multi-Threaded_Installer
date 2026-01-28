#pragma once

#include <string>
#include <vector>
#include <functional>

#include "common/types.h"

namespace MultiThreadedInstaller {

class MetadataParser;
class InstallerPathResolver;

struct FolderTiming {
    double totalSec = 0.0;
    double readSec = 0.0;
    double decompressSec = 0.0;
    double writeSec = 0.0;
    double processSec = 0.0;
    bool indexed = false;
    std::string folderName;
};

struct ParallelInstallSummary {
    double indexedReadSec = 0.0;
    double indexedDecompressSec = 0.0;
    double indexedWriteSec = 0.0;
    double legacyTotalSec = 0.0;
    std::vector<FolderTiming> folderTimings;
};

struct ParallelInstallResult {
    bool success = false;
    std::string installRootPath;
    std::vector<std::string> installedRoots;
    std::vector<std::string> errors;
    ParallelInstallSummary timing;
};

using ProgressCallback = std::function<void(const std::string&, const std::string&, float)>;
using LogCallback = std::function<void(const std::string&)>;

ParallelInstallResult RunParallelInstall(const ExtendedInstallationMetadata& metadata,
                                         MetadataParser& parser,
                                         InstallerPathResolver& pathResolver,
                                         const std::string& userSelectedPath,
                                         const std::vector<std::pair<std::string, std::string>>& folderMappings,
                                         int threadCount,
                                         const ProgressCallback& progressCallback,
                                         const LogCallback& infoCallback,
                                         const LogCallback& errorCallback);

} // namespace MultiThreadedInstaller
