#pragma once

#include "common/types.h"
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/**
 *
 * 
 *
 *
 */
class ConfigurationValidator {
public:
    /**
     *
     */
    struct ValidationResult {
        bool isValid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        
        ValidationResult() : isValid(true) {}
    };
    
    ConfigurationValidator() = default;
    ~ConfigurationValidator() = default;
    
    /**
     *
     *
     *
     *
     */
    ValidationResult validate(const PackagerConfiguration& config,
                             const std::string& inputDirectory);
    
private:
    /**
     *
     *
     *
     *
     */
    bool validateApplicationName(const std::string& name,
                                std::vector<std::string>& errors);
    
    /**
     *
     *
     *
     *
     *
     */
    bool validateFolderExists(const std::string& folder,
                             const std::string& inputDir,
                             std::vector<std::string>& errors);
    
    /**
     *
     *
     *
     *
     */
    bool validateTargetDirectory(const std::string& targetDir,
                                std::vector<std::string>& errors);
};

} // namespace MultiThreadedInstaller
