#ifdef GUI_ENABLED

#include "../../include/gui/installation_worker.h"
#include "../../include/gui/gui_manager.h"
#include "../../include/installer/metadata_parser.h"
#include "../../include/installer/decompression_engine.h"
#include "../../include/installer/thread_pool_manager.h"
#include "../../include/installer/file_system_operator.h"
#include "../../include/installer/path_resolver.h"
#include "../../include/installer/installer_helpers.h"
#include "../../include/installer/install_state_utils.h"
#include "../../include/installer/registry_utils.h"
#include "../../include/installer/uninstall_manager.h"

#include <codecvt>
#include <locale>
#include <filesystem>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <iostream>

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

InstallationWorker::InstallationWorker(HWND hNotifyWindow)
    : m_hNotifyWindow(hNotifyWindow)
    , m_running(false)
    , m_cancellationRequested(false)
    , m_autoRun(false)
    , m_desktopIcons(false)
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

void InstallationWorker::StartInstallation(const std::wstring& installPath, bool autoRun, bool desktopIcons) {
    // 如果已经在运行，不启动新的安装
    if (m_running) {
        return;
    }
    
    // 重置取消标志
    m_cancellationRequested = false;
    m_running = true;
    m_autoRun = autoRun;
    m_desktopIcons = desktopIcons;
    
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

void InstallationWorker::ProgressCallback(const std::string& folder, float progress, void* userData) {
    // 将void*转换回InstallationWorker指针
    InstallationWorker* worker = static_cast<InstallationWorker*>(userData);
    if (worker) {
        // 转换字符串并发送进度消息
        std::wstring wFolder = worker->StringToWString(folder);
        uint64_t total = worker->m_totalBytes.load();
        if (total == 0) {
            worker->PostProgressMessage(wFolder, progress * 100.0f);
            return;
        }
        uint64_t base = worker->m_currentBaseBytes.load();
        uint64_t current = worker->m_currentFolderBytes.load();
        double overall = (static_cast<double>(base) + progress * static_cast<double>(current)) /
                         static_cast<double>(total);
        if (overall > 1.0) {
            overall = 1.0;
        }
        worker->PostProgressMessage(wFolder, static_cast<float>(overall * 100.0));
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
    
    try {
        // 发送初始进度消息
        PostProgressMessage(L"正在准备安装...", 0.0f);
        
        // 解析嵌入的元数据
        MetadataParser parser;
        metadata = parser.parseExtendedEmbeddedMetadata();
        
        if (!parser.validateMetadata(metadata)) {
            throw std::runtime_error("Invalid or corrupted installer metadata");
        }
        metadataValid = true;

        metadata.autoStartup = m_autoRun;
        metadata.desktopIcons = m_desktopIcons;
        
        // 检查取消请求
        if (m_cancellationRequested) {
            throw std::runtime_error("Installation cancelled by user");
        }
        
        // 获取安装状态互斥锁
        if (metadata.installState.useMutex) {
            installMutex = acquireInstallMutex(metadata.installState);
        }
        
        // 应用安装状态
        applyInstallState(metadata.installState, "installing", pathResolver);
        
        // 创建线程池
        auto threadPool = std::make_shared<ThreadPoolManager>(
            std::thread::hardware_concurrency()
        );
        
        // 创建解压引擎
        DecompressionEngine decompressor;
        decompressor.setThreadPool(threadPool);
        
        // 注册进度回调（使用静态回调函数和this指针）
        decompressor.registerProgressCallback(
            [this](const std::string& folder, float progress) {
                ProgressCallback(folder, progress, this);
            }
        );
        
        // 创建文件系统操作器
        FileSystemOperator fsOperator;
        
        // 转换安装路径为string
        std::string installPathStr = WStringToString(installPath);
        
        // 处理每个文件夹
        std::vector<std::string> errors;
        std::mutex errorsMutex;
        std::atomic<bool> overallSuccess(true);

        uint64_t totalBytes = 0;
        for (const auto& mapping : metadata.extendedMappings) {
            totalBytes += mapping.originalSize;
        }
        m_totalBytes = totalBytes;
        m_completedBytes = 0;
        m_currentFolderBytes = 0;
        m_currentBaseBytes = 0;
        
        for (size_t i = 0; i < metadata.extendedMappings.size(); ++i) {
            // 检查取消请求
            if (m_cancellationRequested) {
                throw std::runtime_error("Installation cancelled by user");
            }
            
            const auto& mapping = metadata.extendedMappings[i];
            m_currentFolderBytes = mapping.originalSize;
            m_currentBaseBytes = m_completedBytes.load();
            
            // 确定目标路径
            std::string targetPath;
            if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                // 使用用户选择的安装目录
                targetPath = pathResolver.resolveFinalPath(
                    installPathStr,
                    mapping.targetDirType,
                    metadata.applicationName
                );
            } else {
                // 使用环境变量路径
                std::string basePath = pathResolver.resolveFinalPath(
                    mapping.customTargetPath.empty() ? mapping.targetPath : mapping.customTargetPath,
                    mapping.targetDirType,
                    metadata.applicationName
                );
                
                // 将文件夹名称附加到基础路径
                if (!basePath.empty()) {
                    if (basePath.back() != '\\' && basePath.back() != '/') {
                        basePath += '\\';
                    }
                    targetPath = basePath + mapping.folderName;
                }
            }
            
            if (targetPath.empty()) {
                std::string error = "No target path specified for folder: " + mapping.folderName;
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
                continue;
            }

            if (installRootPath.empty() && mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                installRootPath = targetPath;
            }
            installedRoots.push_back(targetPath);
            
            // 创建目标目录
            if (!fsOperator.createDirectoryRecursive(targetPath)) {
                std::string error = "Failed to create target directory: " + targetPath;
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
                continue;
            }
            
            // 读取压缩数据
            std::vector<uint8_t> compressedData = parser.readCompressedData(
                mapping.offset, 
                mapping.compressedSize
            );
            
            if (compressedData.empty()) {
                std::string error = "Failed to read compressed data for folder: " + mapping.folderName;
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
                continue;
            }
            
            // 创建解压任务
            DecompressionTask task;
            task.compressedData = std::move(compressedData);
            task.targetPath = targetPath;
            task.expectedChecksum = mapping.checksum;
            task.originalSize = mapping.originalSize;
            task.algorithm = mapping.algorithm;
            
            // 执行解压
            bool folderSuccess = decompressor.decompressFolder(task);
            
            if (!folderSuccess) {
                std::string error = "Failed to decompress folder: " + mapping.folderName;
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
            } else {
                m_completedBytes += mapping.originalSize;
            }
        }
        m_currentFolderBytes = 0;
        
        // 检查是否有错误
        if (!overallSuccess) {
            // 合并所有错误消息
            std::string allErrors;
            for (const auto& err : errors) {
                if (!allErrors.empty()) {
                    allErrors += "\n";
                }
                allErrors += err;
            }
            throw std::runtime_error(allErrors);
        }
        
        if (!metadata.registry.empty()) {
            applyRegistryEntries(metadata.registry,
                                 installPathStr,
                                 metadata.configVersion,
                                 metadata.applicationName);
        }

        if (overallSuccess) {
            if ((metadata.autoStartup || metadata.desktopIcons) && installRootPath.empty()) {
                std::cout << "WARNING: Install root not detected; AutoStartup/DesktopIcons skipped" << std::endl;
            }

            if (!installRootPath.empty()) {
                std::filesystem::path exePath = findPrimaryExecutable(installRootPath, metadata.applicationName);
                if ((metadata.autoStartup || metadata.desktopIcons) && exePath.empty()) {
                    std::cout << "WARNING: No executable found for AutoStartup/DesktopIcons" << std::endl;
                } else {
                    if (metadata.autoStartup) {
                        setAutoStartup(metadata.applicationName, exePath);
                    }
                    if (metadata.desktopIcons) {
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
                          metadata.installState, uninstallPath);

            if (!installRootPath.empty()) {
                std::filesystem::path localPath = std::filesystem::path(installRootPath) / "install.manifest.json";
                writeManifest(localPath.string(), metadata.applicationName, metadata.configVersion,
                              installRootPath, installedFiles, metadata.registry,
                              metadata.autoStartup, metadata.desktopIcons,
                              metadata.installState, uninstallPath);
            }

            if (!metadata.registry.empty()) {
                applyRegistryEntries(metadata.registry,
                                     installRootPath,
                                     metadata.configVersion,
                                     metadata.applicationName);
            }
        }

        // 更新安装状态为已完成
        applyInstallState(metadata.installState, "installed", pathResolver);
        
        // 释放互斥锁
        if (installMutex) {
            releaseInstallMutex(installMutex);
        }
        
        // 安装成功
        success = true;
        errorMessage = L"";
        
    } catch (const std::exception& e) {
        // 捕获异常并记录错误
        success = false;
        errorMessage = StringToWString(e.what());
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
