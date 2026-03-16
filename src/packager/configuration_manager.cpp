#include "packager/configuration_manager.h"
#include "common/utf8_utils.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace MultiThreadedInstaller {

bool ConfigurationManager::initialize(const std::string& inputDirectory) {
    lastError_.clear();
    

    auto configOpt = loader_.loadConfiguration(inputDirectory);
    auto applyInstallStateDefaults = [](PackagerConfiguration& config) {
        const std::string identity = config.appId.empty() ? config.applicationName : config.appId;
        if (config.installState.registryPath.empty()) {
            config.installState.registryPath = "HKEY_CURRENT_USER\\Software\\" + identity;
        }
        if (config.installState.filePath.empty()) {
            config.installState.filePath = "%ProgramData%\\" + identity + "\\install.state";
        }
        if (config.installState.mutexName.empty()) {
            config.installState.mutexName = "Global\\" + identity + "_Install";
        }
    };
    
    if (configOpt.has_value()) {

        hasConfigFile_ = true;
        configFilePath_ = loader_.getLoadedConfigPath();
        config_ = configOpt.value();
        applyInstallStateDefaults(config_);
        

        auto validationResult = validator_.validate(config_, inputDirectory);
        
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

        hasConfigFile_ = false;
        configFilePath_.clear();
        config_ = PackagerConfiguration();
        applyInstallStateDefaults(config_);
        

        std::string loaderError = loader_.getLastError();
        if (!loaderError.empty()) {

            lastError_ = "Failed to load configuration: " + loaderError;
            return false;
        }
        
        return true;
    }
}

void ConfigurationManager::applyFolderTargets(std::vector<FolderInfo>& folders) {

    if (config_.folderTargets.empty()) {
        return;
    }
    

    for (auto& folder : folders) {

        fs::path sourcePath = PathFromUtf8(folder.sourcePath);
        std::string folderName = Utf8FromPath(sourcePath.filename());
        

        auto it = std::find_if(config_.folderTargets.begin(), 
                              config_.folderTargets.end(),
                              [&folderName](const FolderTargetConfig& target) {
                                  return target.folderName == folderName;
                              });
        
        if (it != config_.folderTargets.end()) {

            folder.targetPath = it->targetDirectory;
        }

    }
}

} // namespace MultiThreadedInstaller
