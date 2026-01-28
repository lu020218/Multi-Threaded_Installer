#ifdef GUI_ENABLED

#include "../../include/gui/installation_worker.h"
#include "../../include/gui/gui_manager.h"
#include "../../include/gui/gui_helpers.h"
#include "../../include/installer/metadata_parser.h"
#include "../../include/installer/path_resolver.h"
#include "../../include/installer/installer_helpers.h"
#include "../../include/installer/install_state_utils.h"
#include "../../include/installer/registry_utils.h"
#include "../../include/installer/uninstall_manager.h"
#include "../../include/installer/console_interface.h"
#include "../../include/common/installer_parallel_install.h"

#include <codecvt>
#include <locale>
#include <filesystem>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

namespace MultiThreadedInstaller {

// ============================================================================
// 构造函数和析构函数
// ============================================================================

static std::vector<std::string> collectFilesRecursive(const std::string& rootPath) {
    std::vector<std::string> files;
    if (rootPath.empty()) {
        return files;
    }
    std::filesystem::path root(rootPath);
    if (!std::filesystem::exists(root)) {
        return files;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }
    return files;
}

static std::string NormalizePathForCompare(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '/', '\\');
    while (!result.empty() && (result.back() == '\\' || result.back() == '/')) {
        result.pop_back();
    }
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

InstallationWorker::InstallationWorker(HWND hNotifyWindow)
    : m_hNotifyWindow(hNotifyWindow)
    , m_running(false)
    , m_cancellationRequested(false)
    , m_autoRun(false)
    , m_desktopIcons(false)
    , m_cleanupOldInstallRequested(false)
    , m_totalBytes(0)
    , m_completedBytes(0)
    , m_currentFolderBytes(0)
    , m_currentBaseBytes(0) {
}

InstallationWorker::~InstallationWorker() {
    // 请求取消并等待线程结束
    if (m_running) {
        RequestCancellation();
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }
}

// ============================================================================
// 任务 7.1: 实现线程管理
// ============================================================================

void InstallationWorker::StartInstallation(const std::wstring& installPath,
                                           bool autoRun,
                                           bool desktopIcons,
                                           const std::wstring& languageCode,
                                           bool cleanupOldInstall) {
    // 如果已经在运行，不启动新的安装
    if (m_running) {
        return;
    }
    
    // 重置取消标志
    m_cancellationRequested = false;
    m_running = true;
    m_autoRun = autoRun;
    m_desktopIcons = desktopIcons;
    m_languageCode = languageCode;
    m_cleanupOldInstallRequested = cleanupOldInstall;
    
    // 创建工作线程
    m_workerThread = std::thread(&InstallationWorker::WorkerThreadFunc, this, installPath);
}

void InstallationWorker::RequestCancellation() {
    m_cancellationRequested = true;
}

bool InstallationWorker::IsRunning() const {
    return m_running;
}

// ============================================================================
// 任务 7.2: 实现进度回调适配
// ============================================================================

void InstallationWorker::ProgressCallback(const std::string& folder, const std::string& currentFile, float progress, void* userData) {
    // 将void*转换回InstallationWorker指针
    InstallationWorker* worker = static_cast<InstallationWorker*>(userData);
    if (worker) {
        // 转换字符串并发送进度消息
        std::wstring wFolder = worker->StringToWString(folder);
        std::wstring wDisplay = wFolder;
        if (!currentFile.empty()) {
            wDisplay = worker->StringToWString(currentFile);
        }
        uint64_t total = worker->m_totalBytes.load();
        if (total == 0) {
            worker->PostProgressMessage(wDisplay, progress * 100.0f);
            return;
        }
        uint64_t base = worker->m_currentBaseBytes.load();
        uint64_t current = worker->m_currentFolderBytes.load();
        double overall = (static_cast<double>(base) + progress * static_cast<double>(current)) /
                         static_cast<double>(total);
        if (overall > 1.0) {
            overall = 1.0;
        }
        worker->PostProgressMessage(wDisplay, static_cast<float>(overall * 100.0));
    }
}

void InstallationWorker::PostProgressMessage(const std::wstring& folder, float progress) {
    // 分配进度消息数据
    ProgressMessageData* pData = new ProgressMessageData();
    
    // 复制文件夹名称（确保不超过缓冲区大小）
    size_t copyLen = folder.length();
    if (copyLen >= MAX_PATH) {
        copyLen = MAX_PATH - 1;
    }
    wcsncpy_s(pData->currentFolder, MAX_PATH, folder.c_str(), copyLen);
    pData->currentFolder[copyLen] = L'\0';
    
    // 设置进度百分比
    pData->percentage = progress;
    
    // 发送消息到UI线程（UI线程负责释放内存）
    ::PostMessage(m_hNotifyWindow, WM_INSTALLATION_PROGRESS, 0, reinterpret_cast<LPARAM>(pData));
}

void InstallationWorker::PostCompletionMessage(bool success, const std::wstring& errorMsg) {
    // 分配完成消息数据
    CompletionMessageData* pData = new CompletionMessageData();
    
    pData->success = success;
    
    // 复制错误消息（确保不超过缓冲区大小）
    size_t copyLen = errorMsg.length();
    if (copyLen >= 512) {
        copyLen = 511;
    }
    wcsncpy_s(pData->errorMessage, 512, errorMsg.c_str(), copyLen);
    pData->errorMessage[copyLen] = L'\0';
    
    // 发送消息到UI线程（UI线程负责释放内存）
    ::PostMessage(m_hNotifyWindow, WM_INSTALLATION_COMPLETE, 0, reinterpret_cast<LPARAM>(pData));
}

// ============================================================================
// 任务 7.3: 实现安装完成处理
// ============================================================================

void InstallationWorker::WorkerThreadFunc(const std::wstring& installPath) {
    bool success = false;
    std::wstring errorMessage;
    std::string installRootPath;
    std::vector<std::string> installedRoots;
    HANDLE installMutex = nullptr;
    ExtendedInstallationMetadata metadata;
    bool metadataValid = false;
    InstallerPathResolver pathResolver;
    auto startTime = std::chrono::steady_clock::now();
    auto logElapsed = [startTime](const char* label) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        std::cout << "[timing] " << label << " +" << ms << "ms" << std::endl;
    };
    
    try {
        // 发送初始进度消息
        PostProgressMessage(L"正在准备安装...", 0.0f);
        std::cout << "Installation started." << std::endl;
        logElapsed("start");
        
        // 解析嵌入的元数据
        MetadataParser parser;
        metadata = parser.parseExtendedEmbeddedMetadata();
        
        if (!parser.validateMetadata(metadata)) {
            throw std::runtime_error("Invalid or corrupted installer metadata");
        }
        metadataValid = true;
        std::cout << "Metadata loaded. App=" << metadata.applicationName
                  << " folders=" << metadata.folderCount << std::endl;
        logElapsed("metadata_loaded");

        metadata.autoStartup = m_autoRun;
        metadata.desktopIcons = m_desktopIcons;

        std::string installPathStr = WStringToString(installPath);
        if (requiresAdminForInstall(installPathStr, metadata, pathResolver) && !isRunningAsAdmin()) {
#ifdef _WIN32
            SetEnvironmentVariableW(L"MTINSTALLER_INSTALL_PATH", installPath.c_str());
#endif
            if (relaunchSelfAsAdmin()) {
                PostCompletionMessage(false, L"需要管理员权限，已尝试重新启动安装程序。");
            } else {
                PostCompletionMessage(false, L"需要管理员权限，请以管理员身份运行安装程序。");
            }
            return;
        }

        std::string resolvedInstallRoot = pathResolver.resolveFinalPath(
            installPathStr,
            SpecialDirectoryType::INSTALL_DIRECTORY,
            metadata.applicationName
        );
        std::cout << "Install path resolved. input=" << installPathStr
                  << " resolved=" << resolvedInstallRoot << std::endl;
        std::string diskCheckPath = resolvedInstallRoot.empty() ? installPathStr : resolvedInstallRoot;
        uint64_t requiredBytes = 0;
        for (const auto& mapping : metadata.extendedMappings) {
            requiredBytes += mapping.originalSize;
        }
        uint64_t availableBytes = 0;
        if (!checkDiskSpaceForInstall(diskCheckPath, requiredBytes, availableBytes)) {
            PostCompletionMessage(false, L"磁盘空间不足，无法继续安装。");
            return;
        }
        std::cout << "Disk check ok. required=" << requiredBytes
                  << " available=" << availableBytes << std::endl;
        logElapsed("disk_check");

#ifdef _WIN32
        uint16_t currentMajor = 0;
        uint16_t currentMinor = 0;
        uint32_t currentBuild = 0;
        if (!checkMinimumWindowsVersion(metadata.minWindowsMajor,
                                        metadata.minWindowsMinor,
                                        metadata.minWindowsBuild,
                                        currentMajor, currentMinor, currentBuild)) {
            PostCompletionMessage(false, L"系统版本不满足最低要求。");
            return;
        }
        std::cout << "Windows version ok. current=" << currentMajor << "."
                  << currentMinor << "." << currentBuild << std::endl;
        logElapsed("windows_check");
#endif

#ifdef _WIN32
        std::string processName = metadata.applicationName;
        if (!processName.empty()) {
            std::string lower = processName;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".exe") {
                processName += ".exe";
            }
        }
        if (!processName.empty() && isProcessRunningByName(processName)) {
            std::cout << "Running app detected: " << processName << std::endl;
            PostCompletionMessage(false,
                                  GUIHelpers::GetLocalizedText(
                                      L"msg.install.app_running",
                                      L"Application is still running. Please close it before installing."));
            return;
        }
#endif

        {
            std::string previousManifest;
            std::string previousInstallDir;
            if (resolveExistingInstallInfo(metadata.applicationName, pathResolver,
                                           previousManifest, previousInstallDir)) {
                std::string newPath = resolvedInstallRoot.empty() ? installPathStr : resolvedInstallRoot;
                std::string normalizedOld = NormalizePathForCompare(previousInstallDir);
                std::string normalizedNew = NormalizePathForCompare(newPath);
                if (!normalizedOld.empty() && !normalizedNew.empty() &&
                    normalizedOld != normalizedNew) {
                    std::cout << "Detected previous install at: " << previousInstallDir << std::endl;
                    if (previousManifest.empty()) {
                        std::cout << "Old install manifest not found; skipping cleanup." << std::endl;
                    } else if (metadata.autoCleanOldInstall || m_cleanupOldInstallRequested) {
                        ConsoleInterface console;
                        console.showInfo("Cleaning previous installation...");
                        uninstallFromManifest(previousManifest, pathResolver, console);
                        std::cout << "Previous install cleanup done." << std::endl;
                        logElapsed("old_install_cleanup");
                    } else {
                        std::cout << "Skipping cleanup of previous installation." << std::endl;
                    }
                }
            }
        }
        logElapsed("prechecks_complete");

        // 检查取消请求
        if (m_cancellationRequested) {
            throw std::runtime_error("Installation cancelled by user");
        }
        
        // 获取安装状态互斥锁
        if (metadata.installState.useMutex) {
            std::cout << "Acquiring install mutex..." << std::endl;
            installMutex = acquireInstallMutex(metadata.installState);
        }
        
        // 应用安装状态
        applyInstallState(metadata.installState, "installing", pathResolver);
        std::cout << "Install state set to installing." << std::endl;
        
        uint64_t totalBytes = 0;
        std::unordered_map<std::string, uint64_t> folderSizes;
        std::unordered_map<std::string, float> folderProgress;
        for (const auto& mapping : metadata.extendedMappings) {
            totalBytes += mapping.originalSize;
            folderSizes[mapping.folderName] = mapping.originalSize;
            folderProgress[mapping.folderName] = 0.0f;
        }
        std::cout << "Total bytes to install: " << totalBytes << std::endl;
        m_totalBytes = totalBytes;

        std::mutex progressMutex;
        uint64_t lastCompletedBytes = 0;
        uint64_t progressUpdateCount = 0;
        auto lastProgressLog = std::chrono::steady_clock::now();
        auto progressCallback = [this, &folderSizes, &folderProgress, &progressMutex, totalBytes,
                                 &lastCompletedBytes, &progressUpdateCount, &lastProgressLog]
            (const std::string& folder, const std::string& currentFile, float progress) {
            std::lock_guard<std::mutex> lock(progressMutex);
            folderProgress[folder] = progress;
            if (totalBytes == 0) {
                if (!currentFile.empty()) {
                    PostProgressMessage(StringToWString(currentFile), progress * 100.0f);
                } else {
                    PostProgressMessage(StringToWString(folder), progress * 100.0f);
                }
                return;
            }
            double completed = 0.0;
            for (const auto& entry : folderProgress) {
                auto sizeIt = folderSizes.find(entry.first);
                if (sizeIt != folderSizes.end()) {
                    double clamped = std::max(0.0f, std::min(1.0f, entry.second));
                    completed += static_cast<double>(sizeIt->second) * clamped;
                }
            }
            double overall = completed / static_cast<double>(totalBytes);
            if (!currentFile.empty()) {
                PostProgressMessage(StringToWString(currentFile), static_cast<float>(overall * 100.0));
            } else {
                PostProgressMessage(StringToWString(folder), static_cast<float>(overall * 100.0));
            }

            ++progressUpdateCount;
            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressLog).count();
            if (elapsedMs >= 1000) {
                uint64_t completedBytes = static_cast<uint64_t>(completed);
                uint64_t deltaBytes = completedBytes - lastCompletedBytes;
                double elapsedSec = static_cast<double>(elapsedMs) / 1000.0;
                double updatesPerSec = progressUpdateCount / elapsedSec;
                std::cout << "[progress] rate=" << updatesPerSec
                          << " updates/s, delta_bytes=" << deltaBytes
                          << ", percent=" << (overall * 100.0) << std::endl;
                lastCompletedBytes = completedBytes;
                lastProgressLog = now;
                progressUpdateCount = 0;
            }
        };

        auto infoCallback = [](const std::string& message) {
            std::cout << "INFO: " << message << std::endl;
        };
        auto errorCallback = [](const std::string& message) {
            std::cout << "ERROR: " << message << std::endl;
        };

        std::cout << "Decompression engine initialized." << std::endl;
        logElapsed("decompression_init");

        ParallelInstallResult parallelResult = RunParallelInstall(
            metadata,
            parser,
            pathResolver,
            installPathStr,
            {},
            0,
            progressCallback,
            infoCallback,
            errorCallback
        );

        std::cout << "Decompression complete. success="
                  << (parallelResult.success ? "true" : "false") << std::endl;
        logElapsed("decompression_complete");

        if (!parallelResult.success) {
            std::string allErrors;
            for (const auto& err : parallelResult.errors) {
                if (!allErrors.empty()) {
                    allErrors += "\n";
                }
                allErrors += err;
            }
            throw std::runtime_error(allErrors.empty() ? "Installation failed" : allErrors);
        }

        installRootPath = parallelResult.installRootPath;
        installedRoots = std::move(parallelResult.installedRoots);
        
        if (!metadata.registry.empty()) {
            std::cout << "Applying registry entries: " << metadata.registry.size() << std::endl;
            applyRegistryEntries(metadata.registry,
                                 installPathStr,
                                 metadata.configVersion,
                                 metadata.applicationName);
            logElapsed("registry_apply_pre");
        }

        if (parallelResult.success) {
            if ((metadata.autoStartup || metadata.desktopIcons) && installRootPath.empty()) {
                std::cout << "WARNING: Install root not detected; AutoStartup/DesktopIcons skipped" << std::endl;
            }

            if (!installRootPath.empty()) {
                std::filesystem::path exePath = findPrimaryExecutable(installRootPath, metadata.applicationName);
                if ((metadata.autoStartup || metadata.desktopIcons) && exePath.empty()) {
                    std::cout << "WARNING: No executable found for AutoStartup/DesktopIcons" << std::endl;
                } else {
                    if (metadata.autoStartup) {
                        std::cout << "Setting auto-start." << std::endl;
                        setAutoStartup(metadata.applicationName, exePath);
                    }
                    if (metadata.desktopIcons) {
                        std::cout << "Creating desktop shortcut." << std::endl;
                        createDesktopShortcut(metadata.applicationName, exePath);
                    }
                }
            }

            std::vector<std::string> installedFiles;
            for (const auto& root : installedRoots) {
                auto files = collectFilesRecursive(root);
                installedFiles.insert(installedFiles.end(), files.begin(), files.end());
            }
            std::sort(installedFiles.begin(), installedFiles.end());
            installedFiles.erase(std::unique(installedFiles.begin(), installedFiles.end()), installedFiles.end());

            std::string uninstallPath;
            if (!installRootPath.empty()) {
                std::filesystem::path target = std::filesystem::path(installRootPath) / "uninstall.exe";
                std::string currentExe = getCurrentExecutablePath();
                std::error_code ec;
                if (!currentExe.empty() && std::filesystem::exists(currentExe)) {
                    if (createUninstallStub(currentExe, target.string())) {
                        uninstallPath = target.string();
                    } else {
                        std::filesystem::copy_file(currentExe, target,
                                                   std::filesystem::copy_options::overwrite_existing, ec);
                        if (!ec) {
                            uninstallPath = target.string();
                        }
                    }
                }
            }
            if (!uninstallPath.empty()) {
                installedFiles.erase(std::remove(installedFiles.begin(), installedFiles.end(), uninstallPath),
                                     installedFiles.end());
            }

            std::string manifestPath = getDefaultManifestPath(metadata.applicationName, pathResolver);
            writeManifest(manifestPath, metadata.applicationName, metadata.configVersion,
                          installRootPath, installedFiles, metadata.registry,
                          metadata.autoStartup, metadata.desktopIcons,
                          metadata.installState, uninstallPath,
                          WStringToString(m_languageCode));
            std::cout << "Manifest written: " << manifestPath << std::endl;

            if (!installRootPath.empty()) {
                std::filesystem::path localPath = std::filesystem::path(installRootPath) / "install.manifest.json";
                writeManifest(localPath.string(), metadata.applicationName, metadata.configVersion,
                              installRootPath, installedFiles, metadata.registry,
                              metadata.autoStartup, metadata.desktopIcons,
                              metadata.installState, uninstallPath,
                              WStringToString(m_languageCode));
                std::cout << "Local manifest written: " << localPath.string() << std::endl;
            }

            if (!metadata.registry.empty()) {
                std::cout << "Applying registry entries (post-install): "
                          << metadata.registry.size() << std::endl;
                applyRegistryEntries(metadata.registry,
                                     installRootPath,
                                     metadata.configVersion,
                                     metadata.applicationName);
                logElapsed("registry_apply_post");
            }

#ifdef _WIN32
            if (!uninstallPath.empty()) {
                std::cout << "Writing uninstall registry entry." << std::endl;
                bool perMachine = isRunningAsAdmin();
                writeUninstallRegistryEntry(metadata.applicationName,
                                            metadata.configVersion,
                                            installRootPath,
                                            uninstallPath,
                                            perMachine);
                logElapsed("uninstall_registry");
            }
#endif
        }

        // 更新安装状态为已完成
        applyInstallState(metadata.installState, "installed", pathResolver);
        std::cout << "Install state set to installed." << std::endl;
        
        // 释放互斥锁
        if (installMutex) {
            releaseInstallMutex(installMutex);
        }
        
        // 安装成功
        success = true;
        errorMessage = L"";
        std::cout << "Installation completed successfully." << std::endl;
        logElapsed("success");
        
    } catch (const std::exception& e) {
        // 捕获异常并记录错误
        success = false;
        errorMessage = StringToWString(e.what());
        std::cout << "Installation failed: " << e.what() << std::endl;
        logElapsed("failed");
        if (metadataValid) {
            applyInstallState(metadata.installState, "failed", pathResolver);
        }
        if (installMutex) {
            releaseInstallMutex(installMutex);
            installMutex = nullptr;
        }
    } catch (...) {
        // 捕获未知异常
        success = false;
        errorMessage = L"Unknown error occurred during installation";
        std::cout << "Installation failed: unknown error." << std::endl;
        logElapsed("failed_unknown");
        if (metadataValid) {
            applyInstallState(metadata.installState, "failed", pathResolver);
        }
        if (installMutex) {
            releaseInstallMutex(installMutex);
            installMutex = nullptr;
        }
    }
    
    // 标记为不再运行
    m_running = false;
    
    // 发送完成消息
    PostCompletionMessage(success, errorMessage);
}

// ============================================================================
// 辅助函数：字符串转换
// ============================================================================

std::wstring InstallationWorker::StringToWString(const std::string& str) {
    if (str.empty()) {
        return std::wstring();
    }
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string InstallationWorker::WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) {
        return std::string();
    }
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
