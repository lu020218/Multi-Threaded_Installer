#pragma once

#include "log_level.h"
#include "message_formatter.h"
#include <string>
#include <unordered_map>
#include <chrono>
#include <shared_mutex>
#include <vector>

namespace logging {

/**
 * 配置管理器类
 * 负责管理日志系统的所有配置选项，支持从文件、命令行加载配置
 * 以及运行时动态更新配置
 */
class ConfigurationManager {
public:
    /**
     * 日志系统配置结构
     */
    struct LoggingConfig {
        // 全局配置
        LogLevel globalLogLevel = LogLevel::INFO;
        bool verboseMode = false;
        bool colorEnabled = true;
        
        // 控制台配置
        bool consoleEnabled = true;
        LogLevel consoleMinLevel = LogLevel::DEBUG;
        
        // 文件配置
        bool fileEnabled = false;
        std::string filePath = "application.log";
        size_t maxFileSize = 10 * 1024 * 1024;  // 10MB
        int maxFiles = 5;
        bool autoFlush = true;
        
        // 异步配置
        bool asyncEnabled = true;
        size_t bufferSize = 8192;
        std::chrono::milliseconds flushInterval{1000};
        
        // 模块过滤器
        std::unordered_map<std::string, LogLevel> moduleFilters;
        
        // 格式配置
        MessageFormatter::FormatConfig formatConfig;
    };
    
    /**
     * 获取单例实例
     * @return ConfigurationManager的单例引用
     */
    static ConfigurationManager& getInstance();
    
    // 配置加载和保存
    
    /**
     * 从配置文件加载配置
     * @param configPath 配置文件路径
     * @return 加载成功返回true，失败返回false
     */
    bool loadFromFile(const std::string& configPath);
    
    /**
     * 将配置保存到文件
     * @param configPath 配置文件路径
     * @return 保存成功返回true，失败返回false
     */
    bool saveToFile(const std::string& configPath) const;
    
    /**
     * 从命令行参数加载配置
     * @param argc 参数个数
     * @param argv 参数数组
     * @return 解析成功返回true，失败返回false
     */
    bool loadFromCommandLine(int argc, char* argv[]);
    
    // 配置访问
    
    /**
     * 设置完整配置
     * @param config 新的配置
     */
    void setConfig(const LoggingConfig& config);
    
    /**
     * 获取当前配置（只读）
     * @return 当前配置的常量引用
     */
    const LoggingConfig& getConfig() const;
    
    /**
     * 获取当前配置（可修改）
     * @return 当前配置的引用
     */
    LoggingConfig& getConfig();
    
    // 动态配置更新
    
    /**
     * 更新全局日志级别
     * @param level 新的日志级别
     */
    void updateLogLevel(LogLevel level);
    
    /**
     * 更新详细模式设置
     * @param enabled 是否启用详细模式
     */
    void updateVerboseMode(bool enabled);
    
    /**
     * 更新彩色输出设置
     * @param enabled 是否启用彩色输出
     */
    void updateColorEnabled(bool enabled);
    
    /**
     * 更新模块过滤器
     * @param module 模块名称
     * @param level 该模块的最小日志级别
     */
    void updateModuleFilter(const std::string& module, LogLevel level);
    
    /**
     * 移除模块过滤器
     * @param module 模块名称
     */
    void removeModuleFilter(const std::string& module);
    
    /**
     * 清除所有模块过滤器
     */
    void clearModuleFilters();
    
    // 配置验证
    
    /**
     * 验证配置的有效性
     * @param config 要验证的配置
     * @return 配置有效返回true，无效返回false
     */
    bool validateConfig(const LoggingConfig& config) const;
    
    /**
     * 获取配置错误信息
     * @param config 要检查的配置
     * @return 错误信息列表，空列表表示无错误
     */
    std::vector<std::string> getConfigErrors(const LoggingConfig& config) const;
    
private:
    /**
     * 私有构造函数（单例模式）
     */
    ConfigurationManager() = default;
    
    /**
     * 禁用拷贝构造函数
     */
    ConfigurationManager(const ConfigurationManager&) = delete;
    
    /**
     * 禁用赋值操作符
     */
    ConfigurationManager& operator=(const ConfigurationManager&) = delete;
    
    LoggingConfig config_;                    // 当前配置
    mutable std::shared_mutex configMutex_;   // 配置读写锁
    
    /**
     * 应用配置到日志系统
     */
    void applyConfig();
    
    /**
     * 解析配置文件内容
     * @param content 配置文件内容
     * @return 解析后的配置
     */
    LoggingConfig parseConfigFile(const std::string& content);
    
    /**
     * 序列化配置为字符串
     * @param config 要序列化的配置
     * @return 序列化后的字符串
     */
    std::string serializeConfig(const LoggingConfig& config) const;
    
    /**
     * 解析JSON格式的配置
     * @param jsonContent JSON内容
     * @return 解析后的配置
     */
    LoggingConfig parseJsonConfig(const std::string& jsonContent);
    
    /**
     * 将配置序列化为JSON格式
     * @param config 要序列化的配置
     * @return JSON格式的字符串
     */
    std::string serializeToJson(const LoggingConfig& config) const;
    
    /**
     * 解析命令行参数
     * @param argc 参数个数
     * @param argv 参数数组
     * @return 解析后的配置更新
     */
    LoggingConfig parseCommandLineArgs(int argc, char* argv[]);
    
    /**
     * 验证文件路径的有效性
     * @param filePath 文件路径
     * @return 路径有效返回true
     */
    bool validateFilePath(const std::string& filePath) const;
    
    /**
     * 验证缓冲区大小的有效性
     * @param bufferSize 缓冲区大小
     * @return 大小有效返回true
     */
    bool validateBufferSize(size_t bufferSize) const;
    
    /**
     * 验证文件大小限制的有效性
     * @param maxFileSize 最大文件大小
     * @return 大小有效返回true
     */
    bool validateMaxFileSize(size_t maxFileSize) const;
};

} // namespace logging