#pragma once

#include "common/types.h"
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/**
 * ConfigurationValidator - 负责验证配置文件的正确性
 * 
 * 验证配置项的完整性、类型、格式和值的有效性
 * 提供详细的错误和警告信息
 */
class ConfigurationValidator {
public:
    /**
     * 验证结果结构
     */
    struct ValidationResult {
        bool isValid;                       // 是否验证通过
        std::vector<std::string> errors;    // 错误信息列表
        std::vector<std::string> warnings;  // 警告信息列表
        
        ValidationResult() : isValid(true) {}
    };
    
    ConfigurationValidator() = default;
    ~ConfigurationValidator() = default;
    
    /**
     * 验证配置的完整性和正确性
     * @param config 要验证的配置对象
     * @param inputDirectory 输入目录路径（用于验证文件夹存在性）
     * @return 验证结果
     */
    ValidationResult validate(const PackagerConfiguration& config,
                             const std::string& inputDirectory);
    
private:
    /**
     * 验证应用程序名称
     * @param name 应用程序名称
     * @param errors 错误信息列表
     * @return 是否验证通过
     */
    bool validateApplicationName(const std::string& name,
                                std::vector<std::string>& errors);
    
    /**
     * 验证文件夹是否存在
     * @param folder 文件夹名称（相对路径）
     * @param inputDir 输入目录路径
     * @param errors 错误信息列表
     * @return 是否验证通过
     */
    bool validateFolderExists(const std::string& folder,
                             const std::string& inputDir,
                             std::vector<std::string>& errors);
    
    /**
     * 验证目标目录配置
     * @param targetDir 目标目录配置字符串
     * @param errors 错误信息列表
     * @return 是否验证通过
     */
    bool validateTargetDirectory(const std::string& targetDir,
                                std::vector<std::string>& errors);
};

} // namespace MultiThreadedInstaller
