#include "packager/configuration_manager.h"
#include "common/utf8_utils.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace MultiThreadedInstaller {
namespace {

std::string ResolveInstallIdentity(const PackagerConfiguration& config) {
    return config.app.id.empty() ? config.app.name : config.app.id;
}

std::string FolderSourceName(const std::string& source) {
    return Utf8FromPath(PathFromUtf8(source).filename());
}

} // namespace

bool ConfigurationManager::initialize(const std::string& inputDirectory,
                                      const std::string& configDirectory) {
    lastError_.clear();
    

    auto configOpt = loader_.loadConfiguration(configDirectory);
    auto applyInstallDefaults = [](PackagerConfiguration& config) {
        const std::string identity = ResolveInstallIdentity(config);
        if (config.installer.mutex.empty()) {
            config.installer.mutex = "Global\\" + identity + "_Install";
        }
        config.install.mutexName = config.installer.mutex;
        config.install.useMutex = !config.installer.mutex.empty();
    };
    
    if (configOpt.has_value()) {

        hasConfigFile_ = true;
        configFilePath_ = loader_.getLoadedConfigPath();
        config_ = configOpt.value();
        applyInstallDefaults(config_);
        

        auto validationResult = validator_.validate(config_, inputDirectory, configDirectory);
        
        if (!validationResult.isValid) {

            lastError_ = "Configuration validation failed:\n";
            for (const auto& error : validationResult.errors) {
                lastError_ += "  - " + error + "\n";
            }
            return false;
        }
        

        if (!validationResult.warnings.empty()) {


        }
        
        return true;
    } else {

        std::string loaderError = loader_.getLastError();
        lastError_ = loaderError.empty()
                         ? "Failed to load configuration from: " + configDirectory
                         : "Failed to load configuration: " + loaderError;
        return false;
    }
}

void ConfigurationManager::applyFolderTargets(std::vector<FolderInfo>& folders) {
    if (config_.layout.folders.empty()) {
        return;
    }
    for (auto& folder : folders) {
        fs::path sourcePath = PathFromUtf8(folder.sourcePath);
        std::string folderName = Utf8FromPath(sourcePath.filename());

        auto it = std::find_if(
            config_.layout.folders.begin(),
            config_.layout.folders.end(),
            [&folderName](const LayoutFolderConfig& target) {
                return FolderSourceName(target.source) == folderName;
            });

        if (it != config_.layout.folders.end()) {
            folder.id = it->id;
            folder.targetPath = it->target;
        }
    }
}

} // namespace MultiThreadedInstaller
