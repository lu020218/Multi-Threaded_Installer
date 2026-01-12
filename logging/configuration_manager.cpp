#include "common/logging/configuration_manager.h"
#include "common/logging/logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace logging {

ConfigurationManager& ConfigurationManager::getInstance() {
    static ConfigurationManager instance;
    return instance;
}

bool ConfigurationManager::loadFromFile(const std::string& configPath) {
    try {
        if (!std::filesystem::exists(configPath)) {
            return false;
        }
        
        std::ifstream file(configPath);
        if (!file.is_open()) {
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        LoggingConfig newConfig;
        if (configPath.size() >= 5 && configPath.substr(configPath.size() - 5) == ".json") {
            newConfig = parseJsonConfig(content);
        } else {
            newConfig = parseConfigFile(content);
        }
        
        if (!validateConfig(newConfig)) {
            return false;
        }
        
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        config_ = newConfig;
        applyConfig();
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ConfigurationManager::saveToFile(const std::string& configPath) const {
    try {
        std::shared_lock<std::shared_mutex> lock(configMutex_);
        
        std::string content;
        if (configPath.size() >= 5 && configPath.substr(configPath.size() - 5) == ".json") {
            content = serializeToJson(config_);
        } else {
            content = serializeConfig(config_);
        }
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            return false;
        }
        
        file << content;
        return file.good();
    } catch (const std::exception&) {
        return false;
    }
}

bool ConfigurationManager::loadFromCommandLine(int argc, char* argv[]) {
    try {
        LoggingConfig cmdConfig = parseCommandLineArgs(argc, argv);
        
        std::unique_lock<std::shared_mutex> lock(configMutex_);
        
        // 合并命令行配置到当前配置
        if (cmdConfig.globalLogLevel != LogLevel::INFO) {
            config_.globalLogLevel = cmdConfig.globalLogLevel;
        }
        
        if (cmdConfig.verboseMode) {
            config_.verboseMode = cmdConfig.verboseMode;
            config_.globalLogLevel = LogLevel::DEBUG;
        }
        
        if (!cmdConfig.colorEnabled) {
            config_.colorEnabled = cmdConfig.colorEnabled;
        }
        
        if (cmdConfig.fileEnabled) {
            config_.fileEnabled = cmdConfig.fileEnabled;
            if (!cmdConfig.filePath.empty() && cmdConfig.filePath != "application.log") {
                config_.filePath = cmdConfig.filePath;
            }
        }
        
        applyConfig();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void ConfigurationManager::setConfig(const LoggingConfig& config) {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    config_ = config;
    applyConfig();
}

const ConfigurationManager::LoggingConfig& ConfigurationManager::getConfig() const {
    std::shared_lock<std::shared_mutex> lock(configMutex_);
    return config_;
}

ConfigurationManager::LoggingConfig& ConfigurationManager::getConfig() {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    return config_;
}

void ConfigurationManager::updateLogLevel(LogLevel level) {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    config_.globalLogLevel = level;
    applyConfig();
}

void ConfigurationManager::updateVerboseMode(bool enabled) {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    config_.verboseMode = enabled;
    if (enabled) {
        config_.globalLogLevel = LogLevel::DEBUG;
    }
    applyConfig();
}

void ConfigurationManager::updateColorEnabled(bool enabled) {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    config_.colorEnabled = enabled;
    applyConfig();
}

void ConfigurationManager::updateModuleFilter(const std::string& module, LogLevel level) {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    config_.moduleFilters[module] = level;
    applyConfig();
}

void ConfigurationManager::removeModuleFilter(const std::string& module) {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    config_.moduleFilters.erase(module);
    applyConfig();
}

void ConfigurationManager::clearModuleFilters() {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    config_.moduleFilters.clear();
    applyConfig();
}

bool ConfigurationManager::validateConfig(const LoggingConfig& config) const {
    return getConfigErrors(config).empty();
}

std::vector<std::string> ConfigurationManager::getConfigErrors(const LoggingConfig& config) const {
    std::vector<std::string> errors;
    
    // 验证文件路径
    if (config.fileEnabled && !validateFilePath(config.filePath)) {
        errors.push_back("Invalid file path: " + config.filePath);
    }
    
    // 验证缓冲区大小
    if (!validateBufferSize(config.bufferSize)) {
        errors.push_back("Invalid buffer size: " + std::to_string(config.bufferSize));
    }
    
    // 验证最大文件大小
    if (!validateMaxFileSize(config.maxFileSize)) {
        errors.push_back("Invalid max file size: " + std::to_string(config.maxFileSize));
    }
    
    // 验证最大文件数量
    if (config.maxFiles < 1 || config.maxFiles > 100) {
        errors.push_back("Invalid max files count: " + std::to_string(config.maxFiles));
    }
    
    // 验证刷新间隔
    if (config.flushInterval.count() < 100 || config.flushInterval.count() > 60000) {
        errors.push_back("Invalid flush interval: " + std::to_string(config.flushInterval.count()) + "ms");
    }
    
    return errors;
}

void ConfigurationManager::applyConfig() {
    // 这里应该通知Logger和其他组件应用新配置
    // 由于Logger可能还未完全实现，这里先留空
    // 在实际集成时需要调用Logger::getInstance().applyConfig(config_);
}

ConfigurationManager::LoggingConfig ConfigurationManager::parseConfigFile(const std::string& content) {
    LoggingConfig config;
    
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        // 移除注释和空白行
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        
        // 去除首尾空白
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty()) {
            continue;
        }
        
        // 解析键值对
        size_t equalPos = line.find('=');
        if (equalPos == std::string::npos) {
            continue;
        }
        
        std::string key = line.substr(0, equalPos);
        std::string value = line.substr(equalPos + 1);
        
        // 去除键值的空白
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // 解析配置项
        if (key == "log_level") {
            config.globalLogLevel = stringToLogLevel(value);
        } else if (key == "verbose") {
            config.verboseMode = (value == "true" || value == "1");
        } else if (key == "color") {
            config.colorEnabled = (value == "true" || value == "1");
        } else if (key == "console_enabled") {
            config.consoleEnabled = (value == "true" || value == "1");
        } else if (key == "file_enabled") {
            config.fileEnabled = (value == "true" || value == "1");
        } else if (key == "file_path") {
            config.filePath = value;
        } else if (key == "max_file_size") {
            config.maxFileSize = std::stoull(value);
        } else if (key == "max_files") {
            config.maxFiles = std::stoi(value);
        } else if (key == "auto_flush") {
            config.autoFlush = (value == "true" || value == "1");
        } else if (key == "async_enabled") {
            config.asyncEnabled = (value == "true" || value == "1");
        } else if (key == "buffer_size") {
            config.bufferSize = std::stoull(value);
        } else if (key == "flush_interval") {
            config.flushInterval = std::chrono::milliseconds(std::stoi(value));
        }
    }
    
    return config;
}

std::string ConfigurationManager::serializeConfig(const LoggingConfig& config) const {
    std::ostringstream oss;
    
    oss << "# Enhanced Console Logging Configuration\n";
    oss << "# Generated automatically\n\n";
    
    oss << "# Global settings\n";
    oss << "log_level=" << logLevelToString(config.globalLogLevel) << "\n";
    oss << "verbose=" << (config.verboseMode ? "true" : "false") << "\n";
    oss << "color=" << (config.colorEnabled ? "true" : "false") << "\n\n";
    
    oss << "# Console settings\n";
    oss << "console_enabled=" << (config.consoleEnabled ? "true" : "false") << "\n\n";
    
    oss << "# File settings\n";
    oss << "file_enabled=" << (config.fileEnabled ? "true" : "false") << "\n";
    oss << "file_path=" << config.filePath << "\n";
    oss << "max_file_size=" << config.maxFileSize << "\n";
    oss << "max_files=" << config.maxFiles << "\n";
    oss << "auto_flush=" << (config.autoFlush ? "true" : "false") << "\n\n";
    
    oss << "# Async settings\n";
    oss << "async_enabled=" << (config.asyncEnabled ? "true" : "false") << "\n";
    oss << "buffer_size=" << config.bufferSize << "\n";
    oss << "flush_interval=" << config.flushInterval.count() << "\n\n";
    
    oss << "# Module filters\n";
    for (const auto& [module, level] : config.moduleFilters) {
        oss << "module_filter_" << module << "=" << logLevelToString(level) << "\n";
    }
    
    return oss.str();
}

ConfigurationManager::LoggingConfig ConfigurationManager::parseJsonConfig(const std::string& jsonContent) {
    LoggingConfig config;
    
    // 简单的JSON解析实现（生产环境建议使用专业的JSON库）
    // 这里实现基本的键值对解析
    
    std::istringstream stream(jsonContent);
    std::string line;
    
    while (std::getline(stream, line)) {
        // 移除空白和特殊字符
        line.erase(std::remove_if(line.begin(), line.end(), 
            [](char c) { return c == ' ' || c == '\t' || c == '{' || c == '}' || c == ',' || c == '"'; }), 
            line.end());
        
        if (line.empty()) {
            continue;
        }
        
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }
        
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        
        // 解析配置项
        if (key == "globalLogLevel") {
            config.globalLogLevel = stringToLogLevel(value);
        } else if (key == "verboseMode") {
            config.verboseMode = (value == "true");
        } else if (key == "colorEnabled") {
            config.colorEnabled = (value == "true");
        } else if (key == "consoleEnabled") {
            config.consoleEnabled = (value == "true");
        } else if (key == "fileEnabled") {
            config.fileEnabled = (value == "true");
        } else if (key == "filePath") {
            config.filePath = value;
        } else if (key == "maxFileSize") {
            config.maxFileSize = std::stoull(value);
        } else if (key == "maxFiles") {
            config.maxFiles = std::stoi(value);
        } else if (key == "autoFlush") {
            config.autoFlush = (value == "true");
        } else if (key == "asyncEnabled") {
            config.asyncEnabled = (value == "true");
        } else if (key == "bufferSize") {
            config.bufferSize = std::stoull(value);
        } else if (key == "flushInterval") {
            config.flushInterval = std::chrono::milliseconds(std::stoi(value));
        }
    }
    
    return config;
}

std::string ConfigurationManager::serializeToJson(const LoggingConfig& config) const {
    std::ostringstream oss;
    
    oss << "{\n";
    oss << "  \"logging\": {\n";
    oss << "    \"globalLogLevel\": \"" << logLevelToString(config.globalLogLevel) << "\",\n";
    oss << "    \"verboseMode\": " << (config.verboseMode ? "true" : "false") << ",\n";
    oss << "    \"colorEnabled\": " << (config.colorEnabled ? "true" : "false") << ",\n";
    oss << "    \"console\": {\n";
    oss << "      \"enabled\": " << (config.consoleEnabled ? "true" : "false") << ",\n";
    oss << "      \"minLevel\": \"" << logLevelToString(config.consoleMinLevel) << "\"\n";
    oss << "    },\n";
    oss << "    \"file\": {\n";
    oss << "      \"enabled\": " << (config.fileEnabled ? "true" : "false") << ",\n";
    oss << "      \"path\": \"" << config.filePath << "\",\n";
    oss << "      \"maxFileSize\": " << config.maxFileSize << ",\n";
    oss << "      \"maxFiles\": " << config.maxFiles << ",\n";
    oss << "      \"autoFlush\": " << (config.autoFlush ? "true" : "false") << "\n";
    oss << "    },\n";
    oss << "    \"async\": {\n";
    oss << "      \"enabled\": " << (config.asyncEnabled ? "true" : "false") << ",\n";
    oss << "      \"bufferSize\": " << config.bufferSize << ",\n";
    oss << "      \"flushInterval\": " << config.flushInterval.count() << "\n";
    oss << "    },\n";
    oss << "    \"moduleFilters\": {\n";
    
    bool first = true;
    for (const auto& [module, level] : config.moduleFilters) {
        if (!first) {
            oss << ",\n";
        }
        oss << "      \"" << module << "\": \"" << logLevelToString(level) << "\"";
        first = false;
    }
    
    oss << "\n    },\n";
    oss << "    \"format\": {\n";
    oss << "      \"timestampFormat\": \"" << config.formatConfig.timestampFormat << "\",\n";
    oss << "      \"includeThreadId\": " << (config.formatConfig.includeThreadId ? "true" : "false") << ",\n";
    oss << "      \"includeModuleName\": " << (config.formatConfig.includeModuleName ? "true" : "false") << ",\n";
    oss << "      \"includeSourceLocation\": " << (config.formatConfig.includeSourceLocation ? "true" : "false") << "\n";
    oss << "    }\n";
    oss << "  }\n";
    oss << "}\n";
    
    return oss.str();
}

ConfigurationManager::LoggingConfig ConfigurationManager::parseCommandLineArgs(int argc, char* argv[]) {
    LoggingConfig config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--verbose" || arg == "-v") {
            config.verboseMode = true;
            config.globalLogLevel = LogLevel::DEBUG;
        } else if (arg == "--quiet" || arg == "-q") {
            config.globalLogLevel = LogLevel::ERROR;
        } else if (arg == "--no-color") {
            config.colorEnabled = false;
        } else if (arg == "--log-file" && i + 1 < argc) {
            config.fileEnabled = true;
            config.filePath = argv[++i];
        } else if (arg == "--log-level" && i + 1 < argc) {
            config.globalLogLevel = stringToLogLevel(argv[++i]);
        } else if (arg == "--no-async") {
            config.asyncEnabled = false;
        } else if (arg == "--buffer-size" && i + 1 < argc) {
            config.bufferSize = std::stoull(argv[++i]);
        }
    }
    
    return config;
}

bool ConfigurationManager::validateFilePath(const std::string& filePath) const {
    if (filePath.empty()) {
        return false;
    }
    
    // 检查路径是否包含非法字符
    const std::string invalidChars = "<>:\"|?*";
    for (char c : invalidChars) {
        if (filePath.find(c) != std::string::npos) {
            return false;
        }
    }
    
    // 检查父目录是否存在或可创建
    std::filesystem::path path(filePath);
    std::filesystem::path parentDir = path.parent_path();
    
    if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
        try {
            std::filesystem::create_directories(parentDir);
        } catch (const std::exception&) {
            return false;
        }
    }
    
    return true;
}

bool ConfigurationManager::validateBufferSize(size_t bufferSize) const {
    // 缓冲区大小应该在1KB到1GB之间
    return bufferSize >= 1024 && bufferSize <= 1024 * 1024 * 1024;
}

bool ConfigurationManager::validateMaxFileSize(size_t maxFileSize) const {
    // 最大文件大小应该在1MB到10GB之间
    return maxFileSize >= 1024 * 1024 && maxFileSize <= 10ULL * 1024 * 1024 * 1024;
}

} // namespace logging