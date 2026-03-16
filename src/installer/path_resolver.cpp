#include "installer/path_resolver.h"
#include "common/utf8_utils.h"
#include <algorithm>
#include <cctype>
#include <windows.h>

namespace MultiThreadedInstaller {

std::string InstallerPathResolver::resolveFinalPath(
    const std::string& userSelectedPath,
    SpecialDirectoryType targetDirType,
    const std::string& directoryName,
    bool appendDirectoryName) {
    
    std::string basePath;
    

    if (targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
        basePath = userSelectedPath;
    } else {
        basePath = getSpecialDirectoryPath(targetDirType);
    }
    

    basePath = expandEnvironmentVariables(basePath);
    

    basePath = normalizePath(basePath);
    

    if (!appendDirectoryName) {
        return basePath;
    }
    return appendAppNameIfNeeded(basePath, directoryName);
}

std::string InstallerPathResolver::expandEnvironmentVariables(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    std::wstring wide = Utf8ToWide(path);
    if (wide.empty()) {
        return path;
    }

    DWORD size = ExpandEnvironmentStringsW(wide.c_str(), nullptr, 0);
    if (size == 0) {
        return path;
    }

    std::wstring expanded;
    expanded.resize(size);
    DWORD written = ExpandEnvironmentStringsW(wide.c_str(), expanded.data(), size);
    if (written == 0) {
        return path;
    }
    if (!expanded.empty() && expanded.back() == L'\0') {
        expanded.pop_back();
    }
    return WideToUtf8(expanded);
}

bool InstallerPathResolver::pathContainsAppName(
    const std::string& path,
    const std::string& appName) {
    
    if (path.empty() || appName.empty()) {
        return false;
    }
    
    std::string lastDir = getLastDirectoryName(path);
    

    std::string lastDirLower = lastDir;
    std::string appNameLower = appName;
    
    std::transform(lastDirLower.begin(), lastDirLower.end(), lastDirLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(appNameLower.begin(), appNameLower.end(), appNameLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    return lastDirLower == appNameLower;
}

std::string InstallerPathResolver::appendAppNameIfNeeded(
    const std::string& basePath,
    const std::string& appName) {
    
    if (basePath.empty() || appName.empty()) {
        return basePath;
    }
    

    if (pathContainsAppName(basePath, appName)) {
        return basePath;
    }
    

    std::string result = normalizePath(basePath);
    

    if (!result.empty() && result.back() != '\\' && result.back() != '/') {
        result += '\\';
    }
    
    result += appName;
    
    return result;
}

std::string InstallerPathResolver::getSpecialDirectoryPath(SpecialDirectoryType dirType) {
    switch (dirType) {
        case SpecialDirectoryType::PROGRAM_FILES:
            return "%ProgramFiles%";
        case SpecialDirectoryType::PROGRAM_FILES_X86:
            return "%ProgramFiles(x86)%";
        case SpecialDirectoryType::APPDATA_ROAMING:
            return "%AppData%";
        case SpecialDirectoryType::APPDATA_LOCAL:
            return "%LocalAppData%";
        case SpecialDirectoryType::PROGRAM_DATA:
            return "%ProgramData%";
        case SpecialDirectoryType::USER_PROFILE:
            return "%USERPROFILE%";
        case SpecialDirectoryType::INSTALL_DIRECTORY:
        default:
            return "";
    }
}

std::string InstallerPathResolver::normalizePath(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    
    std::string result = path;
    

    while (!result.empty() && (result.back() == '\\' || result.back() == '/')) {
        result.pop_back();
    }
    
    return result;
}

std::string InstallerPathResolver::getLastDirectoryName(const std::string& path) {
    if (path.empty()) {
        return "";
    }
    
    std::string normalized = normalizePath(path);
    

    size_t lastSlash = normalized.find_last_of("\\/");
    
    if (lastSlash == std::string::npos) {

        return normalized;
    }
    

    return normalized.substr(lastSlash + 1);
}

} // namespace MultiThreadedInstaller
