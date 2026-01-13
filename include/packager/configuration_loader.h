#pragma once

#include "common/types.h"
#include <string>
#include <optional>

namespace MultiThreadedInstaller {

/**
 * ConfigurationLoader - 负责查找和加载配置文件
 * 
 * 支持从输入目录或指定路径加载JSON格式的配置文件
 * 按优先级查找配置文件：packager.json -> .packager.json
 * 支持通过环境变量PACKAGER_CONFIG指定配置文件路径
 */
class ConfigurationLoader {
public:
    ConfigurationLoader() = default;
    ~ConfigurationLoader() = default;

    /**
     * 从输入目录加载配置文件
     * @param inputDirectory 输入目录路径
     * @return 配置对象，如果加载失败返回std::nullopt
     */
    std::optional<PackagerConfiguration> loadConfiguration(
        const std::string& inputDirectory);
    
    /**
     * 从指定路径加载配置文件
     * @param configPath 配置文件完整路径
     * @return 配置对象，如果加载失败返回std::nullopt
     */
    std::optional<PackagerConfiguration> loadConfigurationFromPath(
        const std::string& configPath);
    
    /**
     * 获取最后的错误信息
     * @return 错误信息字符串
     */
    std::string getLastError() const { return lastError_; }
    
    /**
     * 获取加载的配置文件路径
     * @return 配置文件路径
     */
    std::string getLoadedConfigPath() const { return loadedConfigPath_; }
    
private:
    std::string lastError_;
    std::string loadedConfigPath_;
    
    /**
     * 查找配置文件（按优先级：packager.json -> .packager.json）
     * @param directory 目录路径
     * @return 配置文件路径，如果未找到返回std::nullopt
     */
    std::optional<std::string> findConfigFile(const std::string& directory);
    
    /**
     * 解析JSON配置文件
     * @param filePath 配置文件路径
     * @return 配置对象，如果解析失败返回std::nullopt
     */
    std::optional<PackagerConfiguration> parseJsonConfig(
        const std::string& filePath);
    
    /**
     * 解析目标目录类型字符串
     * @param dirStr 目录类型字符串
     * @return 目录类型枚举
     */
    SpecialDirectoryType parseDirectoryType(const std::string& dirStr);
};

} // namespace MultiThreadedInstaller
