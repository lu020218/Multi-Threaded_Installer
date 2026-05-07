#pragma once

#include "common/config_types.h"
#include <json.hpp>
#include <string>
#include <optional>

namespace MultiThreadedInstaller {

/**
 *
 * 
 *
 *
 *
 */
class ConfigurationLoader {
public:
    ConfigurationLoader() = default;
    ~ConfigurationLoader() = default;

    /**
     *
     *
     *
     */
    std::optional<PackagerConfiguration> loadConfiguration(
        const std::string& configDirectory);
    
    /**
     *
     *
     *
     */
    std::optional<PackagerConfiguration> loadConfigurationFromPath(
        const std::string& configPath);
    
    /**
     *
     *
     */
    std::string getLastError() const { return lastError_; }
    
    /**
     *
     *
     */
    std::string getLoadedConfigPath() const { return loadedConfigPath_; }
    
private:
    std::string lastError_;
    std::string loadedConfigPath_;
    
    /**
     *
     *
     *
     */
    std::optional<std::string> findConfigFile(const std::string& directory);
    
    /**
     *
     *
     *
     */
    std::optional<PackagerConfiguration> parseYamlConfig(
        const std::string& filePath);

    std::optional<PackagerConfiguration> parseConfigObject(
        const nlohmann::json& configObject,
        const std::string& filePath,
        const std::string& formatLabel);
};

} // namespace MultiThreadedInstaller
