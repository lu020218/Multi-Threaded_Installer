#pragma once

#include "common/config_types.h"
#include "packager/configuration_loader.h"
#include "packager/configuration_validator.h"
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/**
 *
 * 
 *
 *
 */
class ConfigurationManager {
public:
    ConfigurationManager() = default;
    ~ConfigurationManager() = default;
    
    /**
     *
     *
     *
     */
    bool initialize(const std::string& inputDirectory,
                    const std::string& configDirectory);
    
    /**
     *
     *
     */
    const PackagerConfiguration& getConfiguration() const { return config_; }
    
    /**
     *
     *
     */
    bool hasConfigFile() const { return hasConfigFile_; }
    
    /**
     *
     *
     */
    std::string getConfigFilePath() const { return configFilePath_; }
    
    /**
     *
     *
     */
    std::string getLastError() const { return lastError_; }
    
private:
    PackagerConfiguration config_;
    bool hasConfigFile_ = false;
    std::string configFilePath_;
    std::string lastError_;
    ConfigurationLoader loader_;
    ConfigurationValidator validator_;
};

} // namespace MultiThreadedInstaller
