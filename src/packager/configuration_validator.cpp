#include "packager/configuration_validator.h"
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace MultiThreadedInstaller {

ConfigurationValidator::ValidationResult ConfigurationValidator::validate(
    const PackagerConfiguration& config,
    const std::string& inputDirectory) {
    
    ValidationResult result;
    
    // 验证配置版本
    if (config.version.empty()) {
        result.errors.push_back("ERROR: Missing required field 'Version'\n"
                                "  Reason: Version is required\n"
                                "  Suggestion: Add \"Version\": \"1.0\" to the configuration file");
        result.isValid = false;
    }
    
    // 验证应用程序名称
    if (!validateApplicationName(config.applicationName, result.errors)) {
        result.isValid = false;
    }
    
    // 验证文件夹目标配置
    for (const auto& folderTarget : config.folderTargets) {
        // 验证文件夹是否存在
        if (!validateFolderExists(folderTarget.folderName, inputDirectory, result.errors)) {
            result.isValid = false;
        }
        
        // 验证目标目录配置
        if (!validateTargetDirectory(folderTarget.targetDirectory, result.errors)) {
            result.isValid = false;
        }
    }
    
    // 验证默认安装目录格式
    if (!config.defaultInstallDir.empty()) {
        if (!validateTargetDirectory(config.defaultInstallDir, result.errors)) {
            result.isValid = false;
        }
    } else {
        result.errors.push_back("ERROR: Missing required field 'InstallDir'\n"
                                "  Reason: Default install directory is required\n"
                                "  Suggestion: Add \"InstallDir\": \"%ProgramFiles%\" to the configuration file");
        result.isValid = false;
    }

    // 验证图标文件（可选）
    if (!config.iconPath.empty()) {
        fs::path iconPath(config.iconPath);
        if (!iconPath.is_absolute()) {
            iconPath = fs::path(inputDirectory) / iconPath;
        }
        if (!fs::exists(iconPath)) {
            result.errors.push_back("ERROR: Icon file not found: " + iconPath.string());
            result.isValid = false;
        } else if (iconPath.extension() != ".ico") {
            result.errors.push_back("ERROR: Icon file must be .ico: " + iconPath.string());
            result.isValid = false;
        }
    }
    
    // 验证注册表配置（结构完整性）
    for (const auto& reg : config.registry) {
        if (reg.path.empty() || reg.key.empty()) {
            result.errors.push_back("ERROR: Invalid Registry entry\n"
                                    "  Reason: Registry 'path' and 'key' are required\n"
                                    "  Suggestion: Provide both \"path\" and \"key\" in Registry entries");
            result.isValid = false;
            break;
        }
    }

    // 验证安装状态配置
    if ((config.installState.mode == InstallStateMode::REGISTRY ||
         config.installState.mode == InstallStateMode::BOTH) &&
        config.installState.registryPath.empty()) {
        result.errors.push_back("ERROR: InstallState.RegistryPath is required for Registry mode");
        result.isValid = false;
    }
    
    if ((config.installState.mode == InstallStateMode::FILE ||
         config.installState.mode == InstallStateMode::BOTH) &&
        config.installState.filePath.empty()) {
        result.errors.push_back("ERROR: InstallState.FilePath is required for File mode");
        result.isValid = false;
    }
    
    if (config.installState.useMutex && config.installState.mutexName.empty()) {
        result.errors.push_back("ERROR: InstallState.MutexName is required when UseMutex is true");
        result.isValid = false;
    }
    
    return result;
}

bool ConfigurationValidator::validateApplicationName(
    const std::string& name,
    std::vector<std::string>& errors) {
    
    // 验证应用程序名称不为空
    if (name.empty()) {
        errors.push_back("ERROR: Missing required field 'applicationName'\n"
                        "  Reason: Application name is required\n"
                        "  Suggestion: Add \"applicationName\": \"YourAppName\" to the configuration file");
        return false;
    }
    
    // 验证应用程序名称不包含非法字符
    // Windows文件名非法字符: < > : " / \ | ? *
    const std::string illegalChars = "<>:\"/\\|?*";
    for (char c : name) {
        if (illegalChars.find(c) != std::string::npos) {
            errors.push_back("ERROR: Invalid application name '" + name + "'\n"
                           "  Reason: Application name contains illegal character '" + std::string(1, c) + "'\n"
                           "  Suggestion: Remove illegal characters (< > : \" / \\ | ? *) from the application name");
            return false;
        }
    }
    
    // 验证应用程序名称不包含控制字符
    for (char c : name) {
        if (std::iscntrl(static_cast<unsigned char>(c))) {
            errors.push_back("ERROR: Invalid application name\n"
                           "  Reason: Application name contains control characters\n"
                           "  Suggestion: Remove control characters from the application name");
            return false;
        }
    }
    
    return true;
}

bool ConfigurationValidator::validateFolderExists(
    const std::string& folder,
    const std::string& inputDir,
    std::vector<std::string>& errors) {
    
    if (folder.empty()) {
        errors.push_back("ERROR: Empty folder name in folderTargets\n"
                        "  Reason: Folder name cannot be empty\n"
                        "  Suggestion: Provide a valid folder name");
        return false;
    }
    
    // 构建完整路径
    fs::path folderPath = fs::path(inputDir) / folder;
    
    // 检查文件夹是否存在
    if (!fs::exists(folderPath)) {
        errors.push_back("ERROR: Folder does not exist in input directory\n"
                        "  Folder: " + folder + "\n"
                        "  Input Directory: " + inputDir + "\n"
                        "  Full Path: " + folderPath.string() + "\n"
                        "  Suggestion: Ensure the folder exists in the input directory or remove it from folderTargets");
        return false;
    }
    
    // 检查是否为目录
    if (!fs::is_directory(folderPath)) {
        errors.push_back("ERROR: Path is not a directory\n"
                        "  Path: " + folderPath.string() + "\n"
                        "  Suggestion: Ensure the path points to a directory, not a file");
        return false;
    }
    
    return true;
}

bool ConfigurationValidator::validateTargetDirectory(
    const std::string& targetDir,
    std::vector<std::string>& errors) {
    
    if (targetDir.empty()) {
        errors.push_back("ERROR: Empty target directory\n"
                        "  Reason: Target directory cannot be empty\n"
                        "  Suggestion: Provide a valid target directory");
        return false;
    }
    
    // 检查是否为特殊关键字
    if (targetDir == "installDirectory") {
        return true;
    }
    
    // 检查是否包含有效的环境变量
    const std::vector<std::string> validEnvVars = {
        "%ProgramFiles%",
        "%ProgramFiles(x86)%",
        "%AppData%",
        "%LocalAppData%",
        "%ProgramData%",
        "%USERPROFILE%"
    };
    
    bool hasValidEnvVar = false;
    for (const auto& envVar : validEnvVars) {
        if (targetDir.find(envVar) != std::string::npos) {
            hasValidEnvVar = true;
            break;
        }
    }
    
    // 如果包含%但不是有效的环境变量，报错
    if (targetDir.find('%') != std::string::npos && !hasValidEnvVar) {
        errors.push_back("ERROR: Invalid environment variable in target directory\n"
                        "  Target Directory: " + targetDir + "\n"
                        "  Reason: Unknown or unsupported environment variable\n"
                        "  Suggestion: Use one of the supported environment variables:\n"
                        "    - %ProgramFiles%\n"
                        "    - %ProgramFiles(x86)%\n"
                        "    - %AppData%\n"
                        "    - %LocalAppData%\n"
                        "    - %ProgramData%\n"
                        "    - %USERPROFILE%");
        return false;
    }
    
    // 验证路径格式（检查非法字符，但允许环境变量）
    // 移除环境变量后检查剩余部分
    std::string pathToCheck = targetDir;
    for (const auto& envVar : validEnvVars) {
        size_t pos = pathToCheck.find(envVar);
        if (pos != std::string::npos) {
            pathToCheck.replace(pos, envVar.length(), "");
        }
    }
    
    // 检查非法字符（排除路径分隔符）
    const std::string illegalChars = "<>:\"|?*";
    for (char c : pathToCheck) {
        if (illegalChars.find(c) != std::string::npos) {
            errors.push_back("ERROR: Invalid target directory path\n"
                           "  Target Directory: " + targetDir + "\n"
                           "  Reason: Path contains illegal character '" + std::string(1, c) + "'\n"
                           "  Suggestion: Remove illegal characters (< > : \" | ? *) from the path");
            return false;
        }
    }
    
    return true;
}

} // namespace MultiThreadedInstaller
