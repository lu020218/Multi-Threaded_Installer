#include "packager/configuration_manager.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace MultiThreadedInstaller {

bool ConfigurationManager::initialize(const std::string& inputDirectory) {
    lastError_.clear();
    
    // 尝试加载配置文件
    auto configOpt = loader_.loadConfiguration(inputDirectory);
    auto applyInstallStateDefaults = [](PackagerConfiguration& config) {
        if (config.installState.registryPath.empty()) {
            config.installState.registryPath = "HKEY_CURRENT_USER\\Software\\" + config.applicationName;
        }
        if (config.installState.filePath.empty()) {
            config.installState.filePath = "%ProgramData%\\" + config.applicationName + "\\install.state";
        }
        if (config.installState.mutexName.empty()) {
            config.installState.mutexName = "Global\\" + config.applicationName + "_Install";
        }
    };
    
    if (configOpt.has_value()) {
        // 配置文件存在，进行验证
        hasConfigFile_ = true;
        configFilePath_ = loader_.getLoadedConfigPath();
        config_ = configOpt.value();
        applyInstallStateDefaults(config_);
        
        // 验证配置
        auto validationResult = validator_.validate(config_, inputDirectory);
        
        if (!validationResult.isValid) {
            // 验证失败，记录错误
            lastError_ = "Configuration validation failed:\n";
            for (const auto& error : validationResult.errors) {
                lastError_ += "  - " + error + "\n";
            }
            return false;
        }
        
        // 记录警告（如果有）
        if (!validationResult.warnings.empty()) {
            // 警告不影响初始化成功，但可以记录
            // 这里可以添加日志记录逻辑
        }
        
        return true;
    } else {
        // 配置文件不存在，使用默认配置
        hasConfigFile_ = false;
        configFilePath_.clear();
        config_ = PackagerConfiguration();  // 使用默认值
        applyInstallStateDefaults(config_);
        
        // 检查是否有加载错误（例如JSON解析错误）
        std::string loaderError = loader_.getLastError();
        if (!loaderError.empty()) {
            // 有错误说明配置文件存在但解析失败
            lastError_ = "Failed to load configuration: " + loaderError;
            return false;
        }
        
        return true;
    }
}

void ConfigurationManager::applyFolderTargets(std::vector<FolderInfo>& folders) {
    // 如果没有配置文件夹目标，则不做任何修改
    if (config_.folderTargets.empty()) {
        return;
    }
    
    // 为每个文件夹应用目标配置
    for (auto& folder : folders) {
        // 提取文件夹名称（从sourcePath中获取最后一个目录名）
        fs::path sourcePath(folder.sourcePath);
        std::string folderName = sourcePath.filename().string();
        
        // 查找匹配的文件夹目标配置
        auto it = std::find_if(config_.folderTargets.begin(), 
                              config_.folderTargets.end(),
                              [&folderName](const FolderTargetConfig& target) {
                                  return target.folderName == folderName;
                              });
        
        if (it != config_.folderTargets.end()) {
            // 找到匹配的配置，设置targetPath
            folder.targetPath = it->targetDirectory;
        }
        // 如果没有找到匹配的配置，保持原有的targetPath不变
    }
}

} // namespace MultiThreadedInstaller
