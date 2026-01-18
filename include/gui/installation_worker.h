#pragma once

#ifdef GUI_ENABLED

#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include "../common/types.h"

namespace MultiThreadedInstaller {

// Forward declarations
class DecompressionEngine;
class ThreadPoolManager;
class MetadataParser;
class FileSystemOperator;
class InstallerPathResolver;

/**
 * InstallationWorker类 - 在后台线程执行安装操作
 * 负责管理工作线程、进度回调和与UI线程的通信
 */
class InstallationWorker {
public:
    InstallationWorker(HWND hNotifyWindow);
    ~InstallationWorker();
    
    // 启动安装（在新线程中）
    void StartInstallation(const std::wstring& installPath);
    
    // 请求取消安装
    void RequestCancellation();
    
    // 检查是否正在运行
    bool IsRunning() const;
    
private:
    HWND m_hNotifyWindow;                      // UI窗口句柄
    std::thread m_workerThread;                 // 工作线程
    std::atomic<bool> m_running;                // 运行状态标志
    std::atomic<bool> m_cancellationRequested;  // 取消请求标志
    
    // 工作线程函数
    void WorkerThreadFunc(const std::wstring& installPath);
    
    // 进度回调（从DecompressionEngine调用）
    static void ProgressCallback(const std::string& folder, float progress, void* userData);
    
    // 发送进度消息到UI线程
    void PostProgressMessage(const std::wstring& folder, float progress);
    
    // 发送完成消息到UI线程
    void PostCompletionMessage(bool success, const std::wstring& errorMsg);
    
    // 辅助函数：字符串转换
    std::wstring StringToWString(const std::string& str);
    std::string WStringToString(const std::wstring& wstr);
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
