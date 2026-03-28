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

    void StartUninstall(const std::vector<std::string>& identityCandidates);
    bool Joinable() const;

private:
    void WorkerThreadFunc(const std::vector<std::string>& identityCandidates);
    void PostCompletionMessage(bool success, const std::wstring& errorMsg);

    HWND m_hNotifyWindow;
    std::thread m_thread;
};

} // namespace MultiThreadedInstaller

