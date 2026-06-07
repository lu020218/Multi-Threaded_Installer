#pragma once

#include "common/config_types.h"
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class ConfigurationValidator {
public:
    struct ValidationResult {
        bool isValid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        ValidationResult() : isValid(true) {}
    };

    ConfigurationValidator() = default;
    ~ConfigurationValidator() = default;

    // 校验精简后的 app/package/hooks 配置。inputDirectory 用于确认有可打包内容，
    // configDirectory 用于解析 icon 与 hook 脚本的相对路径。
    ValidationResult validate(const PackagerConfiguration& config,
                              const std::string& inputDirectory,
                              const std::string& configDirectory);
};

} // namespace MultiThreadedInstaller
