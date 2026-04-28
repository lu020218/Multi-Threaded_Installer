#pragma once

#include <Windows.h>
#include <string>
#include <thread>
#include <vector>

namespace MultiThreadedInstaller {

class UninstallWorker {
public:
    explicit UninstallWorker(HWND hNotifyWindow);
    ~UninstallWorker();

    void StartUninstall(const std::string& manifestPath);
    bool Joinable() const;

private:
    void WorkerThreadFunc(const std::string& manifestPath);
    void PostProgressMessage(float progress, const std::wstring& currentItem);
    void PostCompletionMessage(bool success, const std::wstring& errorMsg);

    HWND m_hNotifyWindow;
    std::thread m_thread;
};

} // namespace MultiThreadedInstaller

