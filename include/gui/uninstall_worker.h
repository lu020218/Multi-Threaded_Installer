#pragma once

#include <Windows.h>
#include <string>
#include <thread>

namespace MultiThreadedInstaller {

class UninstallWorker {
public:
    explicit UninstallWorker(HWND hNotifyWindow);
    ~UninstallWorker();

    void StartUninstall(const std::string& appName);

private:
    void WorkerThreadFunc(const std::string& appName);
    void PostCompletionMessage(bool success, const std::wstring& errorMsg);

    HWND m_hNotifyWindow;
    std::thread m_thread;
};

} // namespace MultiThreadedInstaller

