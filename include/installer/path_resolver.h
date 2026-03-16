#pragma once

#include <string>
#include "common/types.h"

namespace MultiThreadedInstaller {

/**
 *
 * 
 *
 *
 *
 *
 */
class InstallerPathResolver {
public:
    InstallerPathResolver() = default;
    ~InstallerPathResolver() = default;
    
    /**
     *
     * 
     *
     *
     *
     *
     */
    std::string resolveFinalPath(
        const std::string& userSelectedPath,
        SpecialDirectoryType targetDirType,
        const std::string& directoryName,
        bool appendDirectoryName = true);
    
    /**
     *
     * 
     *
     * - %ProgramFiles%
     * - %ProgramFiles(x86)%
     * - %AppData%
     * - %LocalAppData%
     * - %ProgramData%
     * - %USERPROFILE%
     * 
     *
     *
     */
    std::string expandEnvironmentVariables(const std::string& path);
    
    /**
     *
     * 
     *
     *
     *
     */
    bool pathContainsAppName(const std::string& path, const std::string& appName);
    
    /**
     *
     * 
     *
     *
     *
     */
    std::string appendAppNameIfNeeded(const std::string& basePath, const std::string& appName);
    
private:
    /**
     *
     * 
     *
     *
     */
    std::string getSpecialDirectoryPath(SpecialDirectoryType dirType);
    
    /**
     *
     * 
     *
     *
     */
    std::string normalizePath(const std::string& path);
    
    /**
     *
     * 
     *
     *
     */
    std::string getLastDirectoryName(const std::string& path);
};

} // namespace MultiThreadedInstaller
