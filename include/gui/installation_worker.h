#pragma once

#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include "../common/config_types.h"

namespace MultiThreadedInstaller {

// Forward declarations
class DecompressionEngine;
class ThreadPoolManager;
class MetadataParser;
class FileSystemOperator;
class InstallerPathResolver;

/**
 *
 *
 */
class InstallationWorker {
public:
    InstallationWorker(HWND hNotifyWindow);
    ~InstallationWorker();
    

    void StartInstallation(const std::wstring& installPath,
                           bool autoRun,
                           bool desktopIcons,
                           const std::wstring& languageCode,
                           bool repairMode,
                           bool cleanupOldInstall,
                           const std::vector<std::string>& selectedComponents);
    

    void RequestCancellation();
    

    bool IsRunning() const;

    bool Joinable() const;
    
private:
    HWND m_hNotifyWindow;
    std::thread m_workerThread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_cancellationRequested;
    bool m_autoRun;
    bool m_desktopIcons;
    bool m_repairMode;
    bool m_cleanupOldInstallRequested;
    std::wstring m_languageCode;
    std::vector<std::string> m_selectedComponents;
    std::atomic<uint64_t> m_totalBytes;
    std::atomic<uint64_t> m_completedBytes;
    std::atomic<uint64_t> m_currentFolderBytes;
    std::atomic<uint64_t> m_currentBaseBytes;
    

    void WorkerThreadFunc(const std::wstring& installPath);
    

    static void ProgressCallback(const std::string& folder, const std::string& currentFile, float progress, void* userData);
    

    void PostProgressMessage(const std::wstring& folder, float progress);
    

    void PostCompletionMessage(bool success, const std::wstring& errorMsg);

    void JoinFinishedThreadIfNeeded();
    

};

} // namespace MultiThreadedInstaller

