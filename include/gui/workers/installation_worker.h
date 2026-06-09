#pragma once

#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include "common/config_types.h"

namespace MultiThreadedInstaller {

// Forward declarations
class DecompressionEngine;
class ThreadPoolManager;
class MetadataParser;
class FileSystemOperator;
class InstallerPathResolver;

/// 安装后台 worker：在独立线程调用 ExecuteInstallService 执行安装，通过 PostMessage
/// 把进度/完成回传给 GUI 窗口（m_hNotifyWindow），避免阻塞 UI 线程。
class InstallationWorker {
public:
    /// @param hNotifyWindow 接收 WM_INSTALLATION_* 消息的窗口。
    InstallationWorker(HWND hNotifyWindow);
    ~InstallationWorker();

    /// 在后台线程启动安装。组件参数为历史遗留（单产品单载荷下忽略）。
    void StartInstallation(const std::wstring& installPath,
                           bool autoRun,
                           bool desktopIcons,
                           const std::wstring& languageCode,
                           const std::vector<std::string>& selectedComponents,
                           bool installAllComponents = false,
                           bool upgradeMode = false);

    /// 请求取消（worker 在安全点检查并中止）。
    void RequestCancellation();
    /// 安装线程是否在运行。
    bool IsRunning() const;
    /// 工作线程是否可 join。
    bool Joinable() const;

private:
    HWND m_hNotifyWindow;                       ///< 通知窗口。
    std::thread m_workerThread;                 ///< 安装工作线程。
    std::atomic<bool> m_running;                ///< 是否在运行。
    std::atomic<bool> m_cancellationRequested;  ///< 是否已请求取消。
    bool m_autoRun;                ///< 完成后是否自动运行。
    bool m_desktopIcons;           ///< 是否建桌面快捷方式。
    bool m_installAllComponents;   ///< 组件全装（遗留）。
    bool m_upgradeMode;            ///< 升级模式。
    std::wstring m_languageCode;   ///< 界面语言。
    std::vector<std::string> m_selectedComponents;  ///< 选定组件（遗留）。
    std::atomic<uint64_t> m_totalBytes;          ///< 总字节数（进度计算）。
    std::atomic<uint64_t> m_completedBytes;      ///< 已完成字节数。
    std::atomic<uint64_t> m_currentFolderBytes;  ///< 当前 folder 字节数。
    std::atomic<uint64_t> m_currentBaseBytes;    ///< 当前 folder 起始累计字节。

    void WorkerThreadFunc(const std::wstring& installPath);  ///< 工作线程主体。
    /// 解压进度回调（C 风格，userData 指向本对象）。
    static void ProgressCallback(const std::string& folder, const std::string& currentFile, float progress, void* userData);

    void PostProgressMessage(const std::wstring& folder, float progress);  ///< 投递进度消息。
    void PostProgressMessage(const std::wstring& folder,
                             float progress,
                             const std::wstring& progressPrefix);
    /// 投递完成消息。
    void PostCompletionMessage(bool success, bool rebootRequired, const std::wstring& errorMsg);
    /// 若线程已结束则 join，回收资源。
    void JoinFinishedThreadIfNeeded();
};

} // namespace MultiThreadedInstaller

