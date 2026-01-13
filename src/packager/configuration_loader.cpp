#include "packager/configuration_loader.h"
#include <json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace MultiThreadedInstaller {

std::optional<PackagerConfiguration> ConfigurationLoader::loadConfiguration(
    const std::string& inputDirectory) {
    
    lastError_.clear();
    loadedConfigPath_.clear();
    
    // 首先检查环境变量PACKAGER_CONFIG
    const char* envConfig = std::getenv("PACKAGER_CONFIG");
    if (envConfig != nullptr && std::strlen(envConfig) > 0) {
        std::string envPath(envConfig);
        if (fs::exists(envPath)) {
            return loadConfigurationFromPath(envPath);
        } else {
            lastError_ = "Configuration file specified in PACKAGER_CONFIG does not exist: " + envPath;
            return std::nullopt;
        }
    }
    
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
        fs::path configPath = fs::path(directory) / name;
        if (fs::exists(configPath)) {
            return configPath.string();
        }
    }
    
    return std::nullopt;
}

std::optional<PackagerConfiguration> ConfigurationLoader::parseJsonConfig(
    const std::string& filePath) {
    
    try {
        // 读取文件
        std::ifstream file(filePath);
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
        
        // 解析applicationName（必需字段）
        if (!j.contains("applicationName")) {
            lastError_ = "Missing required field 'applicationName' in configuration file";
            return std::nullopt;
        }
        config.applicationName = j["applicationName"].get<std::string>();
        
        // 解析defaultInstallDirectory（可选）
        if (j.contains("defaultInstallDirectory")) {
            config.defaultInstallDir = j["defaultInstallDirectory"].get<std::string>();
        }
        
        // 解析compressionAlgorithm（可选）
        if (j.contains("compressionAlgorithm")) {
            std::string algo = j["compressionAlgorithm"].get<std::string>();
            if (algo == "zstd") {
                config.compressionAlgorithm = CompressionAlgorithm::ZSTD_FAST;
            } else if (algo == "lzma") {
                config.compressionAlgorithm = CompressionAlgorithm::LZMA_HIGH;
            } else {
                lastError_ = "Invalid compression algorithm: " + algo + " (must be 'zstd' or 'lzma')";
                return std::nullopt;
            }
        }
        
        // 解析folderTargets（可选）
        if (j.contains("folderTargets") && j["folderTargets"].is_array()) {
            for (const auto& target : j["folderTargets"]) {
                FolderTargetConfig ftc;
                
                if (target.contains("folder")) {
                    ftc.folderName = target["folder"].get<std::string>();
                }
                
                if (target.contains("targetDirectory")) {
                    ftc.targetDirectory = target["targetDirectory"].get<std::string>();
                    ftc.dirType = parseDirectoryType(ftc.targetDirectory);
                }
                
                config.folderTargets.push_back(ftc);
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
