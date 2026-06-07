#include "packager/configuration_manager.h"

namespace MultiThreadedInstaller {

bool ConfigurationManager::initialize(const std::string& inputDirectory,
                                      const std::string& configDirectory) {
    lastError_.clear();

    auto configOpt = loader_.loadConfiguration(configDirectory);
    if (!configOpt.has_value()) {
        std::string loaderError = loader_.getLastError();
        lastError_ = loaderError.empty()
                         ? "Failed to load configuration from: " + configDirectory
                         : "Failed to load configuration: " + loaderError;
        return false;
    }

    hasConfigFile_ = true;
    configFilePath_ = loader_.getLoadedConfigPath();
    config_ = configOpt.value();

    auto validationResult = validator_.validate(config_, inputDirectory, configDirectory);
    if (!validationResult.isValid) {
        lastError_ = "Configuration validation failed:\n";
        for (const auto& error : validationResult.errors) {
            lastError_ += "  - " + error + "\n";
        }
        return false;
    }
    return true;
}

} // namespace MultiThreadedInstaller
