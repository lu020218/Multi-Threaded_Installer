#include "packager/configuration_loader.h"
#include "common/utf8_utils.h"
#include <json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace MultiThreadedInstaller {

std::optional<PackagerConfiguration> ConfigurationLoader::loadConfiguration(
    const std::string& inputDirectory) {
    
    lastError_.clear();
    loadedConfigPath_.clear();
    
    // 首先检查环境变量PACKAGER_CONFIG
#ifdef _WIN32
    const wchar_t* envConfigW = _wgetenv(L"PACKAGER_CONFIG");
    if (envConfigW && envConfigW[0] != L'\0') {
        std::string envPath = WideToUtf8(envConfigW);
        if (fs::exists(PathFromUtf8(envPath))) {
            return loadConfigurationFromPath(envPath);
        } else {
            lastError_ = "Configuration file specified in PACKAGER_CONFIG does not exist: " + envPath;
            return std::nullopt;
        }
    }
#else
    const char* envConfig = std::getenv("PACKAGER_CONFIG");
    if (envConfig != nullptr && std::strlen(envConfig) > 0) {
        std::string envPath(envConfig);
        if (fs::exists(PathFromUtf8(envPath))) {
            return loadConfigurationFromPath(envPath);
        } else {
            lastError_ = "Configuration file specified in PACKAGER_CONFIG does not exist: " + envPath;
            return std::nullopt;
        }
    }
#endif
    
    // 在输入目录中查找配置文件
    auto configPath = findConfigFile(inputDirectory);
    if (!configPath) {
        // 配置文件不存在不是错误，返回nullopt让调用者使用默认配置
        return std::nullopt;
    }
    
    return loadConfigurationFromPath(*configPath);
}

std::optional<PackagerConfiguration> ConfigurationLoader::loadConfigurationFromPath(
    const std::string& configPath) {
    
    lastError_.clear();
    loadedConfigPath_ = configPath;
    
    return parseJsonConfig(configPath);
}

std::optional<std::string> ConfigurationLoader::findConfigFile(
    const std::string& directory) {
    
    // 按优先级查找配置文件
    std::vector<std::string> configNames = {
        "packager.json",
        ".packager.json"
    };
    
    for (const auto& name : configNames) {
        fs::path configPath = PathFromUtf8(directory) / name;
        if (fs::exists(configPath)) {
            return Utf8FromPath(configPath);
        }
    }
    
    return std::nullopt;
}

std::optional<PackagerConfiguration> ConfigurationLoader::parseJsonConfig(
    const std::string& filePath) {
    
    try {
        // 读取文件
        std::ifstream file(PathFromUtf8(filePath));
        if (!file.is_open()) {
            lastError_ = "Failed to open configuration file: " + filePath;
            return std::nullopt;
        }
        
        // 解析JSON
        json j;
        try {
            file >> j;
        } catch (const json::parse_error& e) {
            lastError_ = "JSON parse error in " + filePath + ": " + e.what();
            return std::nullopt;
        }
        
        // 创建配置对象（使用默认值）
        PackagerConfiguration config;
        
    // 解析 Version（必需字段）
    if (!j.contains("Version")) {
        lastError_ = "Missing required field 'Version' in configuration file";
        return std::nullopt;
    }
    config.version = j["Version"].get<std::string>();
    
    // 解析 AppName（必需字段）
    if (!j.contains("AppName")) {
        lastError_ = "Missing required field 'AppName' in configuration file";
        return std::nullopt;
    }
    config.applicationName = j["AppName"].get<std::string>();
    
    // 解析 InstallDir（必需字段）
    if (!j.contains("InstallDir")) {
        lastError_ = "Missing required field 'InstallDir' in configuration file";
        return std::nullopt;
    }
    config.defaultInstallDir = j["InstallDir"].get<std::string>();

    // 解析 Icon（可选）
    if (j.contains("Icon") && j["Icon"].is_string()) {
        config.iconPath = j["Icon"].get<std::string>();
    }

    // 解析 WebPageUrl（可选）
    if (j.contains("WebPageUrl") && j["WebPageUrl"].is_string()) {
        config.webPageUrl = j["WebPageUrl"].get<std::string>();
    }

    // 解析版本信息（可选）
    if (j.contains("ProductName") && j["ProductName"].is_string()) {
        config.productName = j["ProductName"].get<std::string>();
    }
    if (j.contains("FileVersion") && j["FileVersion"].is_string()) {
        config.fileVersion = j["FileVersion"].get<std::string>();
    }
    if (j.contains("ProductVersion") && j["ProductVersion"].is_string()) {
        config.productVersion = j["ProductVersion"].get<std::string>();
    }
    if (j.contains("CompanyName") && j["CompanyName"].is_string()) {
        config.companyName = j["CompanyName"].get<std::string>();
    }
    if (j.contains("FileDescription") && j["FileDescription"].is_string()) {
        config.fileDescription = j["FileDescription"].get<std::string>();
    }
    if (j.contains("Copyright") && j["Copyright"].is_string()) {
        config.copyright = j["Copyright"].get<std::string>();
    }
        
        // 解析compressionAlgorithm（可选）
        if (j.contains("compressionAlgorithm")) {
            std::string algo = j["compressionAlgorithm"].get<std::string>();
            if (algo == "lzma") {
                config.compressionAlgorithm = CompressionAlgorithm::LZMA_HIGH;
            } else {
                lastError_ = "Invalid compression algorithm: " + algo + " (must be 'lzma')";
                return std::nullopt;
            }
        }
        
    // 解析 Folder（可选）
    if (j.contains("Folder") && j["Folder"].is_object()) {
        const auto& folderObj = j["Folder"];
        
        if (folderObj.contains("InstallDir") && folderObj["InstallDir"].is_string()) {
            FolderTargetConfig ftc;
            ftc.folderName = folderObj["InstallDir"].get<std::string>();
            ftc.targetDirectory = "installDirectory";
            ftc.dirType = SpecialDirectoryType::INSTALL_DIRECTORY;
            config.folderTargets.push_back(ftc);
        }
        
        if (folderObj.contains("Roaming") && folderObj["Roaming"].is_string()) {
            FolderTargetConfig ftc;
            ftc.folderName = folderObj["Roaming"].get<std::string>();
            ftc.targetDirectory = "%AppData%\\Roaming";
            ftc.dirType = SpecialDirectoryType::APPDATA_ROAMING;
            config.folderTargets.push_back(ftc);
        }
        
        if (folderObj.contains("Local") && folderObj["Local"].is_string()) {
            FolderTargetConfig ftc;
            ftc.folderName = folderObj["Local"].get<std::string>();
            ftc.targetDirectory = "%LocalAppData%";
            ftc.dirType = SpecialDirectoryType::APPDATA_LOCAL;
            config.folderTargets.push_back(ftc);
        }
    }
    
    // 解析 Registry（可选）
    if (j.contains("Registry") && j["Registry"].is_array()) {
        for (const auto& entry : j["Registry"]) {
            if (!entry.is_object()) {
                continue;
            }
            RegistryEntry reg;
            if (entry.contains("path") && entry["path"].is_string()) {
                reg.path = entry["path"].get<std::string>();
            }
            if (entry.contains("key") && entry["key"].is_string()) {
                reg.key = entry["key"].get<std::string>();
            }
            if (entry.contains("value")) {
                if (entry["value"].is_number_integer() || entry["value"].is_number_unsigned()) {
                    reg.type = RegistryValueType::DWORD;
                    reg.value = std::to_string(entry["value"].get<uint32_t>());
                } else if (entry["value"].is_string()) {
                    reg.value = entry["value"].get<std::string>();
                }
            }
            if (entry.contains("type") && entry["type"].is_string()) {
                std::string type = entry["type"].get<std::string>();
                std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                if (type == "dword") {
                    reg.type = RegistryValueType::DWORD;
                } else if (type == "expand" || type == "expand_string") {
                    reg.type = RegistryValueType::EXPAND_STRING;
                } else {
                    reg.type = RegistryValueType::STRING;
                }
            }
            config.registry.push_back(reg);
        }
    }
    
    // 解析 InstallState（可选）
    config.installState.registryPath = "HKEY_CURRENT_USER\\Software\\" + config.applicationName;
    config.installState.filePath = "%ProgramData%\\" + config.applicationName + "\\install.state";
    config.installState.mutexName = "Global\\" + config.applicationName + "_Install";
    
    if (j.contains("InstallState") && j["InstallState"].is_object()) {
        const auto& state = j["InstallState"];
        if (state.contains("Mode") && state["Mode"].is_string()) {
            std::string mode = state["Mode"].get<std::string>();
            std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
            if (mode == "registry") {
                config.installState.mode = InstallStateMode::REGISTRY;
            } else if (mode == "file") {
                config.installState.mode = InstallStateMode::FILE;
            } else if (mode == "both") {
                config.installState.mode = InstallStateMode::BOTH;
            }
        }
        if (state.contains("RegistryPath") && state["RegistryPath"].is_string()) {
            config.installState.registryPath = state["RegistryPath"].get<std::string>();
        }
        if (state.contains("RegistryKey") && state["RegistryKey"].is_string()) {
            config.installState.registryKey = state["RegistryKey"].get<std::string>();
        }
        if (state.contains("FilePath") && state["FilePath"].is_string()) {
            config.installState.filePath = state["FilePath"].get<std::string>();
        }
        if (state.contains("UseMutex") && state["UseMutex"].is_boolean()) {
            config.installState.useMutex = state["UseMutex"].get<bool>();
        }
    if (state.contains("MutexName") && state["MutexName"].is_string()) {
        config.installState.mutexName = state["MutexName"].get<std::string>();
    }

    // 解析 AutoCleanOldInstall（可选）
    if (j.contains("AutoCleanOldInstall") && j["AutoCleanOldInstall"].is_boolean()) {
        config.autoCleanOldInstall = j["AutoCleanOldInstall"].get<bool>();
    }
    }
    
    // 解析 AutoStartup（可选）
    if (j.contains("AutoStartup") && j["AutoStartup"].is_boolean()) {
        config.autoStartup = j["AutoStartup"].get<bool>();
    }
    
    // 解析 DesktopIcons（可选）
    if (j.contains("DesktopIcons") && j["DesktopIcons"].is_boolean()) {
        config.desktopIcons = j["DesktopIcons"].get<bool>();
    }

    // 解析 RequireAdmin（可选）
    if (j.contains("RequireAdmin") && j["RequireAdmin"].is_boolean()) {
        config.requireAdmin = j["RequireAdmin"].get<bool>();
    }

    // 解析 MinWindowsVersion（可选，例如 "10.0.19041"）
    if (j.contains("MinWindowsVersion") && j["MinWindowsVersion"].is_string()) {
        std::string versionText = j["MinWindowsVersion"].get<std::string>();
        std::vector<int> parts;
        size_t start = 0;
        while (start < versionText.size()) {
            size_t end = versionText.find('.', start);
            if (end == std::string::npos) {
                end = versionText.size();
            }
            std::string token = versionText.substr(start, end - start);
            if (token.empty()) {
                lastError_ = "Invalid MinWindowsVersion: empty segment";
                return std::nullopt;
            }
            try {
                parts.push_back(std::stoi(token));
            } catch (...) {
                lastError_ = "Invalid MinWindowsVersion: must be numeric (e.g. 10.0.19041)";
                return std::nullopt;
            }
            start = end + 1;
        }

        if (parts.empty() || parts.size() > 3) {
            lastError_ = "Invalid MinWindowsVersion: expected format 'major.minor.build'";
            return std::nullopt;
        }
        config.minWindowsMajor = static_cast<uint16_t>(parts[0]);
        config.minWindowsMinor = static_cast<uint16_t>(parts.size() > 1 ? parts[1] : 0);
        config.minWindowsBuild = static_cast<uint32_t>(parts.size() > 2 ? parts[2] : 0);
    }

    // 解析 SparseFileThresholdBytes（可选）
    if (j.contains("SparseFileThresholdBytes")) {
        if (j["SparseFileThresholdBytes"].is_number_unsigned() ||
            j["SparseFileThresholdBytes"].is_number_integer()) {
            config.sparseFileThresholdBytes = j["SparseFileThresholdBytes"].get<uint64_t>();
        } else {
            lastError_ = "Invalid SparseFileThresholdBytes: must be an integer byte size";
            return std::nullopt;
        }
    }
        
    return config;
        
    } catch (const std::exception& e) {
        lastError_ = "Error parsing configuration file: " + std::string(e.what());
        return std::nullopt;
    }
}

SpecialDirectoryType ConfigurationLoader::parseDirectoryType(
    const std::string& dirStr) {
    
    // 检查是否为installDirectory
    if (dirStr == "installDirectory") {
        return SpecialDirectoryType::INSTALL_DIRECTORY;
    }
    
    // 检查是否包含环境变量
    if (dirStr.find("%ProgramFiles%") != std::string::npos) {
        return SpecialDirectoryType::PROGRAM_FILES;
    }
    
    if (dirStr.find("%AppData%") != std::string::npos) {
        return SpecialDirectoryType::APPDATA_ROAMING;
    }
    
    if (dirStr.find("%LocalAppData%") != std::string::npos) {
        return SpecialDirectoryType::APPDATA_LOCAL;
    }
    
    if (dirStr.find("%ProgramData%") != std::string::npos) {
        return SpecialDirectoryType::PROGRAM_DATA;
    }
    
    // 默认返回INSTALL_DIRECTORY
    return SpecialDirectoryType::INSTALL_DIRECTORY;
}

} // namespace MultiThreadedInstaller
