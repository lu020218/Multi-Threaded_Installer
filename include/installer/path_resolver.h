#pragma once

#include <string>
#include "common/types.h"

namespace MultiThreadedInstaller {

/**
 * @brief 安装程序路径解析器
 * 
 * 负责解析安装路径，包括：
 * - 展开Windows环境变量
 * - 检测路径是否包含应用程序名
 * - 智能补齐应用程序名称目录
 */
class InstallerPathResolver {
public:
    InstallerPathResolver() = default;
    ~InstallerPathResolver() = default;
    
    /**
     * @brief 解析最终的安装路径
     * 
     * @param userSelectedPath 用户选择的安装目录
     * @param targetDirType 目标目录类型
     * @param applicationName 应用程序名称
     * @return 解析后的完整路径
     */
    std::string resolveFinalPath(
        const std::string& userSelectedPath,
        SpecialDirectoryType targetDirType,
        const std::string& applicationName);
    
    /**
     * @brief 展开Windows环境变量
     * 
     * 支持的环境变量：
     * - %ProgramFiles%
     * - %ProgramFiles(x86)%
     * - %AppData%
     * - %LocalAppData%
     * - %ProgramData%
     * - %USERPROFILE%
     * 
     * @param path 包含环境变量的路径
     * @return 展开后的路径
     */
    std::string expandEnvironmentVariables(const std::string& path);
    
    /**
     * @brief 检查路径的最后一个目录是否为应用程序名
     * 
     * @param path 要检查的路径
     * @param appName 应用程序名称
     * @return true 如果路径已包含应用程序名
     */
    bool pathContainsAppName(const std::string& path, const std::string& appName);
    
    /**
     * @brief 如果需要，追加应用程序名到路径
     * 
     * @param basePath 基础路径
     * @param appName 应用程序名称
     * @return 补齐后的路径
     */
    std::string appendAppNameIfNeeded(const std::string& basePath, const std::string& appName);
    
private:
    /**
     * @brief 获取特殊目录的基础路径
     * 
     * @param dirType 目录类型
     * @return 基础路径（可能包含环境变量）
     */
    std::string getSpecialDirectoryPath(SpecialDirectoryType dirType);
    
    /**
     * @brief 规范化路径（移除尾部斜杠等）
     * 
     * @param path 原始路径
     * @return 规范化后的路径
     */
    std::string normalizePath(const std::string& path);
    
    /**
     * @brief 提取路径的最后一个目录名
     * 
     * @param path 路径
     * @return 最后一个目录名
     */
    std::string getLastDirectoryName(const std::string& path);
};

} // namespace MultiThreadedInstaller
