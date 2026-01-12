#pragma once

#include "log_sink.h"
#include "message_formatter.h"
#include <fstream>
#include <mutex>
#include <chrono>
#include <filesystem>

namespace logging {

/**
 * 文件输出器
 * 实现线程安全的文件日志输出，支持文件轮转、自动刷新和缓冲机制
 */
class FileSink : public LogSink {
public:
    /**
     * 文件输出器配置结构
     */
    struct Config {
        std::string filePath;                                    // 日志文件路径
        size_t maxFileSize = 10 * 1024 * 1024;                 // 最大文件大小（默认10MB）
        int maxFiles = 5;                                       // 最大文件数量
        bool autoFlush = true;                                  // 是否自动刷新
        std::chrono::milliseconds flushInterval{1000};         // 刷新间隔（毫秒）
        
        /**
         * 验证配置的有效性
         * @return 如果配置有效则返回true
         */
        bool isValid() const {
            return !filePath.empty() && 
                   maxFileSize > 0 && 
                   maxFiles > 0 && 
                   flushInterval.count() > 0;
        }
    };
    
    /**
     * 构造函数
     * @param config 文件输出器配置
     */
    explicit FileSink(const Config& config);
    
    /**
     * 构造函数
     * @param name 输出器名称
     * @param config 文件输出器配置
     */
    FileSink(const std::string& name, const Config& config);
    
    /**
     * 析构函数
     */
    ~FileSink() override;
    
    /**
     * 写入日志消息
     * @param message 要写入的日志消息
     */
    void write(const LogMessage& message) override;
    
    /**
     * 刷新输出缓冲区
     */
    void flush() override;
    
    /**
     * 检查是否线程安全
     * @return 始终返回true，因为FileSink是线程安全的
     */
    bool isThreadSafe() const override { return true; }
    
    /**
     * 手动轮转文件
     */
    void rotateFile();
    
    /**
     * 检查是否需要轮转文件
     * @return 如果需要轮转则返回true
     */
    bool needsRotation() const;
    
    /**
     * 更新配置
     * @param config 新的配置
     */
    void updateConfig(const Config& config);
    
    /**
     * 获取当前配置
     * @return 当前配置的引用
     */
    const Config& getConfig() const { return config_; }
    
    /**
     * 获取当前文件大小
     * @return 当前文件大小（字节）
     */
    size_t getCurrentFileSize() const;
    
    /**
     * 检查文件是否打开
     * @return 如果文件已打开则返回true
     */
    bool isFileOpen() const;

private:
    Config config_;                                             // 配置信息
    std::ofstream fileStream_;                                  // 文件输出流
    mutable std::mutex fileMutex_;                              // 文件操作互斥锁
    size_t currentFileSize_;                                    // 当前文件大小
    std::chrono::steady_clock::time_point lastFlush_;          // 上次刷新时间
    MessageFormatter formatter_;                                // 消息格式器
    
    /**
     * 打开文件
     * @return 如果成功打开则返回true
     */
    bool openFile();
    
    /**
     * 关闭文件
     */
    void closeFile();
    
    /**
     * 生成轮转后的文件名
     * @param index 文件索引
     * @return 轮转后的文件名
     */
    std::string generateRotatedFileName(int index);
    
    /**
     * 清理旧的日志文件
     */
    void cleanupOldFiles();
    
    /**
     * 检查是否需要刷新
     * @return 如果需要刷新则返回true
     */
    bool needsFlush() const;
    
    /**
     * 创建目录（如果不存在）
     * @param filePath 文件路径
     * @return 如果成功创建或目录已存在则返回true
     */
    bool createDirectoryIfNeeded(const std::string& filePath);
    
    /**
     * 获取文件大小
     * @param filePath 文件路径
     * @return 文件大小，如果文件不存在则返回0
     */
    size_t getFileSize(const std::string& filePath) const;
};

} // namespace logging