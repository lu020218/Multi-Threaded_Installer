#pragma once

#include "common/config_types.h"
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/// 打包配置校验器：检查 app/package/hooks 各字段合法、icon/hook 脚本存在、输入目录有内容。
class ConfigurationValidator {
public:
    /// 校验结果：是否通过 + 错误/警告列表。
    struct ValidationResult {
        bool isValid;                        ///< 是否通过（有 error 则为 false）。
        std::vector<std::string> errors;     ///< 致命错误（阻止打包）。
        std::vector<std::string> warnings;   ///< 警告（不阻止）。

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
