#include "packager/configuration_manager.h"
#include "common/utf8_utils.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace MultiThreadedInstaller {
namespace {

std::string ResolveInstallStateIdentity(const PackagerConfiguration& config) {
    return config.app.id.empty() ? config.app.name : config.app.id;
}

std::string DestinationTypeToPathToken(const LayoutFolderDestination& destination) {
    const std::string type = destination.type;
    if (type == "install") {
        return "installDirectory";
    }
    if (type == "custom") {
        return destination.path;
    }
    if (type == "programFiles") {
        return "%ProgramFiles%";
    }
    if (type == "programFilesX86") {
        return "%ProgramFiles(x86)%";
    }
    if (type == "appDataRoaming") {
        return "%AppData%";
    }
    if (type == "appDataLocal") {
        return "%LocalAppData%";
    }
    if (type == "programData") {
        return "%ProgramData%";
    }
    if (type == "userProfile") {
        return "%USERPROFILE%";
    }
    return destination.path;
}

std::string FolderSourceName(const std::string& source) {
    return Utf8FromPath(PathFromUtf8(source).filename());
}

} // namespace

bool ConfigurationManager::initialize(const std::string& inputDirectory) {
    lastError_.clear();
    

    auto configOpt = loader_.loadConfiguration(inputDirectory);
    auto applyInstallStateDefaults = [](PackagerConfiguration& config) {
        const std::string identity = ResolveInstallStateIdentity(config);
        if (config.install.installState.registryPath.empty()) {
            config.install.installState.registryPath = "HKEY_CURRENT_USER\\Software\\" + identity;
        }
        if (config.install.installState.filePath.empty()) {
            config.install.installState.filePath = "%ProgramData%\\" + identity + "\\install.state";
        }
        if (config.install.installState.mutexName.empty()) {
            config.install.installState.mutexName = "Global\\" + identity + "_Install";
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
            folder.targetPath = DestinationTypeToPathToken(it->destination);
        }
    }
}

} // namespace MultiThreadedInstaller
