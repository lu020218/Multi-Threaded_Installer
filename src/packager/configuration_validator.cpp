#include "packager/configuration_validator.h"
#include "common/utf8_utils.h"
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace MultiThreadedInstaller {

ConfigurationValidator::ValidationResult ConfigurationValidator::validate(
    const PackagerConfiguration& config,
    const std::string& inputDirectory) {
    
    ValidationResult result;
    

    if (config.version.empty()) {
        result.errors.push_back("ERROR: Missing required field 'Version'\n"
                                "  Reason: Version is required\n"
                                "  Suggestion: Add \"Version\": \"1.0\" to the configuration file");
        result.isValid = false;
    }
    

    if (!validateApplicationName(config.applicationName, result.errors)) {
        result.isValid = false;
    }
    

    for (const auto& folderTarget : config.folderTargets) {

        if (!validateFolderExists(folderTarget.folderName, inputDirectory, result.errors)) {
            result.isValid = false;
        }
        

        if (!validateTargetDirectory(folderTarget.targetDirectory, result.errors)) {
            result.isValid = false;
        }
    }
    

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


    if (!config.iconPath.empty()) {
        fs::path iconPath = PathFromUtf8(config.iconPath);
        if (!iconPath.is_absolute()) {
            iconPath = PathFromUtf8(inputDirectory) / iconPath;
        }
        if (!fs::exists(iconPath)) {
            result.errors.push_back("ERROR: Icon file not found: " + Utf8FromPath(iconPath));
            result.isValid = false;
        } else if (iconPath.extension() != ".ico") {
            result.errors.push_back("ERROR: Icon file must be .ico: " + Utf8FromPath(iconPath));
            result.isValid = false;
        }
    }
    

    for (const auto& reg : config.registry) {
        if (reg.path.empty() || reg.key.empty()) {
            result.errors.push_back("ERROR: Invalid Registry entry\n"
                                    "  Reason: Registry 'path' and 'key' are required\n"
                                    "  Suggestion: Provide both \"path\" and \"key\" in Registry entries");
            result.isValid = false;
            break;
        }
    }


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
    

    if (name.empty()) {
        errors.push_back("ERROR: Missing required field 'applicationName'\n"
                        "  Reason: Application name is required\n"
                        "  Suggestion: Add \"applicationName\": \"YourAppName\" to the configuration file");
        return false;
    }
    


    const std::string illegalChars = "<>:\"/\\|?*";
    for (char c : name) {
        if (illegalChars.find(c) != std::string::npos) {
            errors.push_back("ERROR: Invalid application name '" + name + "'\n"
                           "  Reason: Application name contains illegal character '" + std::string(1, c) + "'\n"
                           "  Suggestion: Remove illegal characters (< > : \" / \\ | ? *) from the application name");
            return false;
        }
    }
    

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
    

    fs::path folderPath = PathFromUtf8(inputDir) / PathFromUtf8(folder);
    

    if (!fs::exists(folderPath)) {
        errors.push_back("ERROR: Folder does not exist in input directory\n"
                        "  Folder: " + folder + "\n"
                        "  Input Directory: " + inputDir + "\n"
                        "  Full Path: " + Utf8FromPath(folderPath) + "\n"
                        "  Suggestion: Ensure the folder exists in the input directory or remove it from folderTargets");
        return false;
    }
    

    if (!fs::is_directory(folderPath)) {
        errors.push_back("ERROR: Path is not a directory\n"
                        "  Path: " + Utf8FromPath(folderPath) + "\n"
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
    

    if (targetDir == "installDirectory") {
        return true;
    }
    

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
    


    std::string pathToCheck = targetDir;
    for (const auto& envVar : validEnvVars) {
        size_t pos = pathToCheck.find(envVar);
        if (pos != std::string::npos) {
            pathToCheck.replace(pos, envVar.length(), "");
        }
    }
    

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
