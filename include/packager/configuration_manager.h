#pragma once

#include "common/types.h"
#include "packager/configuration_loader.h"
#include "packager/configuration_validator.h"
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/**
 * ConfigurationManager - 管理配置数据并提供访问接口
 * 
 * 负责初始化配置、验证配置、应用配置到文件夹信息
 * 提供统一的配置访问接口
 */
class ConfigurationManager {
public:
    ConfigurationManager() = default;
    ~ConfigurationManager() = default;
    
    /**
     * 初始化配置管理器
     * @param inputDirectory 输入目录路径
     * @return 是否初始化成功
     */
    bool initialize(const std::string& inputDirectory);
    
    /**
     * 获取配置
     * @return 配置对象的常量引用
     */
    const PackagerConfiguration& getConfiguration() const { return config_; }
    
    /**
     * 检查是否使用了配置文件
     * @return 是否使用了配置文件
     */
    bool hasConfigFile() const { return hasConfigFile_; }
    
    /**
     * 获取配置文件路径
     * @return 配置文件路径
     */
    std::string getConfigFilePath() const { return configFilePath_; }
    
    /**
     * 应用文件夹目标配置到文件夹信息
     * @param folders 文件夹信息列表
     */
    void applyFolderTargets(std::vector<FolderInfo>& folders);
    
    /**
     * 获取最后的错误信息
     * @return 错误信息字符串
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
