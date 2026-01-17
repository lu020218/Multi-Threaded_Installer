#include "installer/metadata_parser.h"
#include "installer/thread_pool_manager.h"
#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include "installer/console_interface.h"
#include "installer/path_resolver.h"
#include <iostream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <memory>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#endif

using namespace MultiThreadedInstaller;

namespace {

struct FileWriter {
    std::string path;
    uint64_t start;
    uint64_t end;
    std::mutex mutex;
};

struct BlockInfo {
    uint32_t blockId;
    uint64_t compressedOffset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint64_t startOffset;
};

struct BlockMetaHeader {
    uint32_t offset;
    uint32_t compressedSize;
    uint32_t originalSize;
    uint32_t checksum;
};

struct BlockSegment {
    size_t fileIndex;
    uint64_t blockOffset;
    uint64_t fileOffset;
    uint64_t size;
};

struct FolderTiming {
    double totalSec = 0.0;
    double readSec = 0.0;
    double decompressSec = 0.0;
    double writeSec = 0.0;
    double processSec = 0.0;
    bool indexed = false;
    std::string folderName;
};

std::filesystem::path toLongPath(const std::filesystem::path& path) {
#ifdef _WIN32
    std::filesystem::path absPath = std::filesystem::absolute(path);
    std::wstring native = absPath.native();
    if (native.rfind(LR"(\\?\)", 0) == 0) {
        return absPath;
    }
    if (native.rfind(LR"(\\)", 0) == 0) {
        std::wstring unc = LR"(\\?\UNC\)" + native.substr(2);
        return std::filesystem::path(unc);
    }
    std::wstring longPath = LR"(\\?\)" + native;
    return std::filesystem::path(longPath);
#else
    return path;
#endif
}

bool ensureFileWithSize(const std::filesystem::path& path, uint64_t size) {
    std::filesystem::path openPath = toLongPath(path);
    std::fstream file(openPath, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!file) {
        std::ofstream create(openPath, std::ios::binary | std::ios::trunc);
        if (!create) {
            return false;
        }
        create.close();
        file.open(openPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) {
            return false;
        }
    }
    
    if (size > 0) {
        file.seekp(static_cast<std::streamoff>(size - 1));
        char zero = 0;
        file.write(&zero, 1);
        file.flush();
    }
    
    return static_cast<bool>(file);
}

bool openFileForWrite(const std::filesystem::path& path, std::fstream& stream) {
    std::filesystem::path openPath = toLongPath(path);
    stream.open(openPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!stream) {
        std::ofstream create(openPath, std::ios::binary | std::ios::app);
        if (!create) {
            return false;
        }
        create.close();
        stream.open(openPath, std::ios::binary | std::ios::in | std::ios::out);
    }
    return static_cast<bool>(stream);
}

std::wstring toWideUtf8(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) {
        return std::wstring();
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), len);
    return wide;
#else
    (void)text;
    return std::wstring();
#endif
}

std::filesystem::path findPrimaryExecutable(const std::filesystem::path& installRoot,
                                            const std::string& appName) {
    std::filesystem::path candidate = installRoot / (appName + ".exe");
    if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
        return candidate;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(installRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".exe") {
            return entry.path();
        }
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(installRoot)) {
        if (!entry.is_directory()) {
            continue;
        }
        for (const auto& fileEntry : std::filesystem::directory_iterator(entry.path())) {
            if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".exe") {
                return fileEntry.path();
            }
        }
    }
    
    return std::filesystem::path();
}

bool setAutoStartup(const std::string& appName, const std::filesystem::path& exePath) {
#ifdef _WIN32
    HKEY key = nullptr;
    LONG status = RegOpenKeyExA(HKEY_CURRENT_USER,
                                "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    
    std::string value = "\"" + exePath.string() + "\"";
    status = RegSetValueExA(key, appName.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>(value.size() + 1));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)appName;
    (void)exePath;
    return false;
#endif
}

bool createDesktopShortcut(const std::string& appName, const std::filesystem::path& exePath) {
#ifdef _WIN32
    PWSTR desktopPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_CREATE, nullptr, &desktopPath);
    if (FAILED(hr) || !desktopPath) {
        return false;
    }
    
    std::wstring linkPath = std::wstring(desktopPath) + L"\\" + toWideUtf8(appName) + L".lnk";
    CoTaskMemFree(desktopPath);
    
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coInit = (hr == S_OK || hr == S_FALSE);
    if (!coInit && hr != RPC_E_CHANGED_MODE) {
        return false;
    }
    
    IShellLinkW* link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                          reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) {
        if (coInit) {
            CoUninitialize();
        }
        return false;
    }
    
    std::wstring targetPath = toWideUtf8(exePath.string());
    std::wstring workingDir = toWideUtf8(exePath.parent_path().string());
    link->SetPath(targetPath.c_str());
    if (!workingDir.empty()) {
        link->SetWorkingDirectory(workingDir.c_str());
    }
    link->SetDescription(toWideUtf8(appName).c_str());
    
    IPersistFile* persist = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
    if (FAILED(hr) || !persist) {
        link->Release();
        if (coInit) {
            CoUninitialize();
        }
        return false;
    }
    
    hr = persist->Save(linkPath.c_str(), TRUE);
    persist->Release();
    link->Release();
    if (coInit) {
        CoUninitialize();
    }
    
    return SUCCEEDED(hr);
#else
    (void)appName;
    (void)exePath;
    return false;
#endif
}

bool applyInstallStateRegistry(const InstallStateConfig& config, const std::string& stateValue) {
#ifdef _WIN32
    if (config.registryPath.empty()) {
        return false;
    }
    
    std::string path = config.registryPath;
    std::string pathUpper = path;
    std::transform(pathUpper.begin(), pathUpper.end(), pathUpper.begin(), ::toupper);
    
    HKEY root = nullptr;
    std::string subkey;
    const std::string hkcu = "HKEY_CURRENT_USER\\";
    const std::string hklm = "HKEY_LOCAL_MACHINE\\";
    const std::string hkcuShort = "HKCU\\";
    const std::string hklmShort = "HKLM\\";
    
    if (pathUpper.rfind(hkcu, 0) == 0) {
        root = HKEY_CURRENT_USER;
        subkey = path.substr(hkcu.size());
    } else if (pathUpper.rfind(hklm, 0) == 0) {
        root = HKEY_LOCAL_MACHINE;
        subkey = path.substr(hklm.size());
    } else if (pathUpper.rfind(hkcuShort, 0) == 0) {
        root = HKEY_CURRENT_USER;
        subkey = path.substr(hkcuShort.size());
    } else if (pathUpper.rfind(hklmShort, 0) == 0) {
        root = HKEY_LOCAL_MACHINE;
        subkey = path.substr(hklmShort.size());
    } else {
        return false;
    }
    
    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG status = RegCreateKeyExA(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disposition);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    
    const std::string& name = config.registryKey.empty() ? std::string("InstallState") : config.registryKey;
    status = RegSetValueExA(key, name.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(stateValue.c_str()),
                            static_cast<DWORD>(stateValue.size() + 1));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)config;
    (void)stateValue;
    return false;
#endif
}

bool applyInstallStateFile(const InstallStateConfig& config, const std::string& stateValue,
                           InstallerPathResolver& resolver) {
    if (config.filePath.empty()) {
        return false;
    }
    
    std::string expandedPath = resolver.expandEnvironmentVariables(config.filePath);
    if (expandedPath.empty()) {
        return false;
    }
    
    std::filesystem::path filePath(expandedPath);
    std::filesystem::path parent = filePath.parent_path();
    if (!parent.empty()) {
        FileSystemOperator fs;
        if (!fs.createDirectoryRecursive(parent.string())) {
            return false;
        }
    }
    
    std::ofstream out(toLongPath(filePath), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(stateValue.c_str(), static_cast<std::streamsize>(stateValue.size()));
    return static_cast<bool>(out);
}

HANDLE acquireInstallMutex(const InstallStateConfig& config) {
#ifdef _WIN32
    if (config.mutexName.empty()) {
        return nullptr;
    }
    std::wstring name = toWideUtf8(config.mutexName);
    if (name.empty()) {
        return nullptr;
    }
    HANDLE handle = CreateMutexW(nullptr, FALSE, name.c_str());
    return handle;
#else
    (void)config;
    return nullptr;
#endif
}

void releaseInstallMutex(HANDLE handle) {
#ifdef _WIN32
    if (handle) {
        CloseHandle(handle);
    }
#else
    (void)handle;
#endif
}

void applyInstallState(const InstallStateConfig& config, const std::string& stateValue,
                       InstallerPathResolver& resolver) {
    if (config.mode == InstallStateMode::REGISTRY || config.mode == InstallStateMode::BOTH) {
        applyInstallStateRegistry(config, stateValue);
    }
    if (config.mode == InstallStateMode::FILE || config.mode == InstallStateMode::BOTH) {
        applyInstallStateFile(config, stateValue, resolver);
    }
}

std::string replaceAll(std::string value, const std::string& token, const std::string& replacement) {
    if (token.empty()) {
        return value;
    }
    size_t pos = 0;
    while ((pos = value.find(token, pos)) != std::string::npos) {
        value.replace(pos, token.length(), replacement);
        pos += replacement.length();
    }
    return value;
}

std::string expandRegistryValue(const std::string& value,
                                const std::string& installDir,
                                const std::string& configVersion,
                                const std::string& appName) {
    std::string result = value;
    result = replaceAll(result, "%InstallDir%", installDir);
    result = replaceAll(result, "%Version%", configVersion);
    result = replaceAll(result, "%AppName%", appName);
    return result;
}

bool writeRegistryValue(const RegistryEntry& entry,
                        const std::string& value,
                        RegistryValueType type) {
#ifdef _WIN32
    if (entry.path.empty() || entry.key.empty()) {
        return false;
    }
    
    std::string path = entry.path;
    std::string pathUpper = path;
    std::transform(pathUpper.begin(), pathUpper.end(), pathUpper.begin(), ::toupper);
    
    HKEY root = nullptr;
    std::string subkey;
    const std::string hkcu = "HKEY_CURRENT_USER\\";
    const std::string hklm = "HKEY_LOCAL_MACHINE\\";
    const std::string hkcuShort = "HKCU\\";
    const std::string hklmShort = "HKLM\\";
    
    if (pathUpper.rfind(hkcu, 0) == 0) {
        root = HKEY_CURRENT_USER;
        subkey = path.substr(hkcu.size());
    } else if (pathUpper.rfind(hklm, 0) == 0) {
        root = HKEY_LOCAL_MACHINE;
        subkey = path.substr(hklm.size());
    } else if (pathUpper.rfind(hkcuShort, 0) == 0) {
        root = HKEY_CURRENT_USER;
        subkey = path.substr(hkcuShort.size());
    } else if (pathUpper.rfind(hklmShort, 0) == 0) {
        root = HKEY_LOCAL_MACHINE;
        subkey = path.substr(hklmShort.size());
    } else {
        return false;
    }
    
    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG status = RegCreateKeyExA(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disposition);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    
    bool ok = false;
    if (type == RegistryValueType::DWORD) {
        try {
            uint32_t number = std::stoul(value, nullptr, 0);
            status = RegSetValueExA(key, entry.key.c_str(), 0, REG_DWORD,
                                    reinterpret_cast<const BYTE*>(&number),
                                    static_cast<DWORD>(sizeof(uint32_t)));
            ok = (status == ERROR_SUCCESS);
        } catch (...) {
            ok = false;
        }
    } else if (type == RegistryValueType::EXPAND_STRING) {
        status = RegSetValueExA(key, entry.key.c_str(), 0, REG_EXPAND_SZ,
                                reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>(value.size() + 1));
        ok = (status == ERROR_SUCCESS);
    } else {
        status = RegSetValueExA(key, entry.key.c_str(), 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>(value.size() + 1));
        ok = (status == ERROR_SUCCESS);
    }
    
    RegCloseKey(key);
    return ok;
#else
    (void)entry;
    (void)value;
    (void)type;
    return false;
#endif
}

void applyRegistryEntries(const std::vector<RegistryEntry>& entries,
                          const std::string& installDir,
                          const std::string& configVersion,
                          const std::string& appName) {
    for (const auto& entry : entries) {
        std::string expanded = expandRegistryValue(entry.value, installDir, configVersion, appName);
        bool ok = writeRegistryValue(entry, expanded, entry.type);
        if (ok) {
            std::cout << "INFO: Registry write ok: " << entry.path
                      << " [" << entry.key << "]=" << expanded << std::endl;
        } else {
            std::cout << "WARNING: Registry write failed: " << entry.path
                      << " [" << entry.key << "]" << std::endl;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    ConsoleInterface console;
    auto startTime = std::chrono::steady_clock::now();
    
    // 解析命令行参数
    auto args = console.parseInstallerArgs(argc, argv);
    
    if (args.showHelp) {
        console.showInstallerHelp();
        return 0;
    }
    
    console.showInfo("Starting installation process...");
    
    // 解析嵌入的扩展元数据
    MetadataParser parser;
    if (!args.dataPackagePath.empty()) {
        parser.setDataPackagePath(args.dataPackagePath);
    }
    auto metadata = parser.parseExtendedEmbeddedMetadata();
    
    if (!parser.validateMetadata(metadata)) {
        console.showError("Invalid or corrupted installer metadata");
        return 1;
    }
    
    console.showInfo("Found " + std::to_string(metadata.folderCount) + " folders to install");
    console.showInfo("Application: " + metadata.applicationName);
    
    // 创建路径解析器
    InstallerPathResolver pathResolver;
    HANDLE installMutex = nullptr;
    if (metadata.installState.useMutex) {
        installMutex = acquireInstallMutex(metadata.installState);
    }
    applyInstallState(metadata.installState, "installing", pathResolver);
    
    // 如果没有提供文件夹映射，使用交互模式
    std::string userSelectedPath;
    std::string installRootPath;
    if (args.folderMappings.empty() && args.defaultDestination.empty()) {
        console.showInstallerMenu();
        
        // 显示默认安装目录建议
        std::string defaultPath = pathResolver.expandEnvironmentVariables(metadata.defaultInstallDir);
        console.showInfo("Suggested installation directory: " + defaultPath);
        
        // 获取用户输入的安装目录
        std::cout << "Enter installation directory (or press Enter to use default): ";
        std::getline(std::cin, userSelectedPath);
        
        if (userSelectedPath.empty()) {
            userSelectedPath = defaultPath;
        }
        
        console.showInfo("Installing to: " + userSelectedPath);
    } else if (!args.defaultDestination.empty()) {
        userSelectedPath = args.defaultDestination;
    }
    
    // 创建线程池
    auto threadPool = std::make_shared<ThreadPoolManager>(
        args.threadCount > 0 ? args.threadCount : std::thread::hardware_concurrency()
    );
    
    // 创建解压引擎
    DecompressionEngine decompressor;
    decompressor.setThreadPool(threadPool);
    decompressor.registerProgressCallback([&console](const std::string& folder, float progress) {
        console.showInstallationProgress(folder, progress);
    });
    
    // 创建文件系统操作器
    FileSystemOperator fsOperator;
    
    std::vector<std::string> errors;
    std::mutex errorsMutex;
    std::mutex progressMutex;
    std::atomic<bool> overallSuccess(true);
    std::atomic<size_t> completedFolders(0);
    std::atomic<long long> totalReadNs(0);
    std::atomic<long long> totalDecompressNs(0);
    std::atomic<long long> totalWriteNs(0);
    std::atomic<long long> totalLegacyNs(0);
    std::mutex timingMutex;
    std::vector<FolderTiming> folderTimings;
    
    // 准备所有文件夹的解压任务
    struct FolderTask {
        std::string folderName;
        std::string targetPath;
        ExtendedFolderMapping mapping;
        bool useIndex = false;
        DecompressionTask decompTask;
        double legacyReadSec = 0.0;
        DecompressionEngine::LegacyStageTiming legacyStage;
    };
    
    std::vector<FolderTask> folderTasks;
    folderTasks.reserve(metadata.extendedMappings.size());
    
    // 第一阶段：准备所有任务（路径解析、目录创建、数据读取）
    for (size_t i = 0; i < metadata.extendedMappings.size(); ++i) {
        const auto& mapping = metadata.extendedMappings[i];
        
        // 确定目标路径
        std::string targetPath;
        bool foundMapping = false;
        
        // 首先检查用户是否为此文件夹指定了特定路径
        for (const auto& userMapping : args.folderMappings) {
            if (userMapping.first == mapping.folderName) {
                targetPath = userMapping.second;
                foundMapping = true;
                break;
            }
        }
        
        // 如果没有找到用户映射，使用路径解析器根据目标目录类型解析路径
        if (!foundMapping) {
            std::string basePath;
            if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                // 使用用户选择的安装目录
                basePath = pathResolver.resolveFinalPath(
                    userSelectedPath,
                    mapping.targetDirType,
                    metadata.applicationName
                );
            } else {
                // 使用环境变量路径
                basePath = pathResolver.resolveFinalPath(
                    mapping.customTargetPath.empty() ? mapping.targetPath : mapping.customTargetPath,
                    mapping.targetDirType,
                    metadata.applicationName
                );
            }
            
            // 将文件夹名称附加到基础路径（安装目录不需要额外层级）
            if (!basePath.empty()) {
                if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                    targetPath = basePath;
                } else {
                    // 确保路径以分隔符结尾
                    if (basePath.back() != '\\' && basePath.back() != '/') {
                        basePath += '\\';
                    }
                    targetPath = basePath + mapping.folderName;
                }
            }
        }

        if (installRootPath.empty() && mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
            installRootPath = targetPath;
        }
        
        if (targetPath.empty()) {
            std::string error = "No target path specified for folder: " + mapping.folderName;
            console.showError(error);
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back(error);
            overallSuccess = false;
            continue;
        }
        
        console.showInfo("Installing folder '" + mapping.folderName + "' to: " + targetPath);
        
        // 创建目标目录
        if (!fsOperator.createDirectoryRecursive(targetPath)) {
            std::string error = "Failed to create target directory: " + targetPath;
            console.showError(error);
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back(error);
            overallSuccess = false;
            continue;
        }
        
        // 创建解压任务
        FolderTask folderTask;
        folderTask.folderName = mapping.folderName;
        folderTask.targetPath = targetPath;
        folderTask.mapping = mapping;
        folderTask.useIndex = !mapping.fileIndex.empty() && !mapping.blockIndex.empty();
        if (folderTask.useIndex) {
            console.showInfo("Install path for '" + mapping.folderName + "': indexed");
        } else {
            console.showInfo("Install path for '" + mapping.folderName + "': legacy");
        }
        
        if (!folderTask.useIndex) {
            auto readStart = std::chrono::steady_clock::now();
            std::vector<uint8_t> compressedData = parser.readCompressedData(mapping.offset, mapping.compressedSize);
            auto readEnd = std::chrono::steady_clock::now();
            if (compressedData.empty()) {
                std::string error = "Failed to read compressed data for folder: " + mapping.folderName;
                console.showError(error);
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
                continue;
            }
            folderTask.legacyReadSec = std::chrono::duration<double>(readEnd - readStart).count();
            
            folderTask.decompTask.compressedData = std::move(compressedData);
            folderTask.decompTask.targetPath = targetPath;
            folderTask.decompTask.expectedChecksum = mapping.checksum;
            folderTask.decompTask.originalSize = mapping.originalSize;
            folderTask.decompTask.algorithm = mapping.algorithm;
        }
        
        folderTasks.push_back(std::move(folderTask));
    }
    
    auto installWithIndex = [&](const FolderTask& folderTask, FolderTiming& timing) -> bool {
        const auto& mapping = folderTask.mapping;
        if (mapping.fileIndex.empty() || mapping.blockIndex.empty()) {
            console.showError("Indexed metadata missing for '" + folderTask.folderName + "'");
            return false;
        }
        
        timing.indexed = true;
        timing.folderName = folderTask.folderName;
        auto totalStart = std::chrono::steady_clock::now();
        
        std::vector<std::unique_ptr<FileWriter>> writers;
        writers.reserve(mapping.fileIndex.size());
        
        uint64_t totalBytes = 0;
        for (const auto& fileEntry : mapping.fileIndex) {
            std::filesystem::path fullPath = std::filesystem::path(folderTask.targetPath) / fileEntry.relativePath;
            FileSystemOperator fsOp;
            std::filesystem::path parent = fullPath.parent_path();
            if (!parent.empty()) {
                if (!fsOp.createDirectoryRecursive(parent.string())) {
                    console.showError("Failed to create directory: " + parent.string());
                    return false;
                }
            }
            
            std::fstream stream;
            if (!ensureFileWithSize(fullPath, fileEntry.size)) {
                console.showError("Failed to create file: " + fullPath.string());
                return false;
            }
            
            auto writer = std::make_unique<FileWriter>();
            writer->path = fullPath.string();
            writer->start = fileEntry.offset;
            writer->end = fileEntry.offset + fileEntry.size;
            writers.push_back(std::move(writer));
            totalBytes += fileEntry.size;
        }
        
        std::vector<FileWriter*> writerPtrs;
        writerPtrs.reserve(writers.size());
        for (const auto& writer : writers) {
            writerPtrs.push_back(writer.get());
        }
        if (writerPtrs.empty()) {
            console.showError("No files to write for '" + folderTask.folderName + "'");
            return false;
        }
        
        std::vector<size_t> fileOrder(writerPtrs.size());
        for (size_t i = 0; i < fileOrder.size(); ++i) {
            fileOrder[i] = i;
        }
        std::sort(fileOrder.begin(), fileOrder.end(),
                  [&](size_t a, size_t b) { return writerPtrs[a]->start < writerPtrs[b]->start; });
        
        std::vector<BlockInfo> blocks;
        bool parsedHeader = false;
        {
            std::vector<uint8_t> headerCount = parser.readCompressedData(mapping.offset, sizeof(uint32_t));
            if (headerCount.size() == sizeof(uint32_t)) {
                uint32_t blockCount = *reinterpret_cast<const uint32_t*>(headerCount.data());
                size_t headerSize = sizeof(uint32_t) + static_cast<size_t>(blockCount) * sizeof(BlockMetaHeader);
                if (blockCount > 0 && headerSize <= static_cast<size_t>(mapping.compressedSize)) {
                    std::vector<uint8_t> headerData = parser.readCompressedData(mapping.offset, headerSize);
                    if (headerData.size() == headerSize) {
                        blocks.reserve(blockCount);
                        size_t metaOffset = sizeof(uint32_t);
                        for (uint32_t i = 0; i < blockCount; ++i) {
                            BlockMetaHeader meta;
                            std::memcpy(&meta, headerData.data() + metaOffset + i * sizeof(BlockMetaHeader),
                                        sizeof(BlockMetaHeader));
                            BlockInfo block;
                            block.blockId = i;
                            block.compressedOffset = meta.offset;
                            block.compressedSize = meta.compressedSize;
                            block.originalSize = meta.originalSize;
                            block.startOffset = 0;
                            blocks.push_back(block);
                        }
                        parsedHeader = true;
                    }
                }
            }
        }
        
        if (!parsedHeader) {
            console.showInfo("Indexed header read failed for '" + folderTask.folderName + "', using metadata index");
            blocks.reserve(mapping.blockIndex.size());
            for (const auto& blockEntry : mapping.blockIndex) {
                BlockInfo block;
                block.blockId = blockEntry.blockId;
                block.compressedOffset = blockEntry.offset;
                block.compressedSize = blockEntry.compressedSize;
                block.originalSize = blockEntry.originalSize;
                block.startOffset = 0;
                blocks.push_back(block);
            }
        }
        if (blocks.empty()) {
            console.showError("No blocks available for '" + folderTask.folderName + "'");
            return false;
        }
        std::sort(blocks.begin(), blocks.end(),
                  [](const BlockInfo& a, const BlockInfo& b) { return a.blockId < b.blockId; });
        
        for (const auto& block : blocks) {
            if (block.compressedOffset + block.compressedSize > mapping.compressedSize) {
                console.showError("Invalid block metadata for '" + folderTask.folderName +
                                  "': block " + std::to_string(block.blockId) +
                                  " out of range");
                return false;
            }
        }
        
        uint64_t cumulative = 0;
        for (auto& block : blocks) {
            block.startOffset = cumulative;
            cumulative += block.originalSize;
        }
        
        std::vector<std::vector<BlockSegment>> segments(blocks.size());
        size_t fileIdx = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            uint64_t blockStart = blocks[i].startOffset;
            uint64_t blockEnd = blockStart + blocks[i].originalSize;
            
            while (fileIdx < fileOrder.size() && writerPtrs[fileOrder[fileIdx]]->end <= blockStart) {
                ++fileIdx;
            }
            
            size_t k = fileIdx;
            while (k < fileOrder.size()) {
                FileWriter* writer = writerPtrs[fileOrder[k]];
                if (writer->start >= blockEnd) {
                    break;
                }
                
                uint64_t overlapStart = std::max(blockStart, writer->start);
                uint64_t overlapEnd = std::min(blockEnd, writer->end);
                if (overlapEnd > overlapStart) {
                    BlockSegment seg;
                    seg.fileIndex = fileOrder[k];
                    seg.blockOffset = overlapStart - blockStart;
                    seg.fileOffset = overlapStart - writer->start;
                    seg.size = overlapEnd - overlapStart;
                    segments[i].push_back(seg);
                }
                
                if (writer->end <= blockEnd) {
                    ++k;
                } else {
                    break;
                }
            }
            
            while (fileIdx < fileOrder.size() && writerPtrs[fileOrder[fileIdx]]->end <= blockEnd) {
                ++fileIdx;
            }
        }
        
        for (auto& segs : segments) {
            std::sort(segs.begin(), segs.end(),
                      [](const BlockSegment& a, const BlockSegment& b) {
                          if (a.fileIndex == b.fileIndex) {
                              return a.fileOffset < b.fileOffset;
                          }
                          return a.fileIndex < b.fileIndex;
                      });
        }
        
        std::atomic<uint64_t> writtenBytes(0);
        std::atomic<long long> readNs(0);
        std::atomic<long long> decompressNs(0);
        std::atomic<long long> writeNs(0);
        
        if (threadPool && threadPool->getTotalThreadCount() > 1) {
            std::atomic<bool> blockFailed(false);
            std::vector<std::future<bool>> futures;
            futures.reserve(blocks.size());
            
            for (size_t i = 0; i < blocks.size(); ++i) {
                futures.push_back(threadPool->enqueue([&, i]() -> bool {
                    if (blockFailed.load()) {
                        return true;
                    }
                    
                    const auto& block = blocks[i];
                    auto readStart = std::chrono::steady_clock::now();
                    std::vector<uint8_t> compressedData = parser.readCompressedData(
                        mapping.offset + block.compressedOffset,
                        block.compressedSize
                    );
                    auto readEnd = std::chrono::steady_clock::now();
                    readNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(readEnd - readStart).count());
                    if (compressedData.empty()) {
                        console.showError("Indexed read failed for '" + folderTask.folderName +
                                          "': block " + std::to_string(block.blockId));
                        blockFailed.store(true);
                        return false;
                    }
                    
                    auto decompressStart = std::chrono::steady_clock::now();
                    std::vector<uint8_t> decompressed;
                    if (!decompressor.decompressLzmaBlockData(compressedData, block.originalSize, decompressed)) {
                        console.showError("Indexed decompress failed for '" + folderTask.folderName +
                                          "': block " + std::to_string(block.blockId));
                        blockFailed.store(true);
                        return false;
                    }
                    auto decompressEnd = std::chrono::steady_clock::now();
                    decompressNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(decompressEnd - decompressStart).count());
                    
                    uint64_t blockWritten = 0;
                    auto writeStart = std::chrono::steady_clock::now();
                    const auto& segs = segments[i];
                    size_t currentFileIndex = static_cast<size_t>(-1);
                    std::fstream stream;
                    std::unique_lock<std::mutex> fileLock;
                    for (const auto& seg : segs) {
                        if (seg.fileIndex != currentFileIndex) {
                            if (stream.is_open()) {
                                stream.close();
                            }
                            if (fileLock.owns_lock()) {
                                fileLock.unlock();
                            }
                            currentFileIndex = seg.fileIndex;
                            FileWriter* writer = writerPtrs[currentFileIndex];
                            fileLock = std::unique_lock<std::mutex>(writer->mutex);
                            if (!openFileForWrite(writer->path, stream)) {
                                console.showError("Indexed write failed for '" + folderTask.folderName +
                                                  "': block " + std::to_string(block.blockId));
                                blockFailed.store(true);
                                return false;
                            }
                        }
                        stream.seekp(static_cast<std::streamoff>(seg.fileOffset));
                        stream.write(reinterpret_cast<const char*>(decompressed.data() + seg.blockOffset),
                                     static_cast<std::streamsize>(seg.size));
                        if (!stream) {
                            console.showError("Indexed write failed for '" + folderTask.folderName +
                                              "': block " + std::to_string(block.blockId));
                            blockFailed.store(true);
                            return false;
                        }
                        blockWritten += seg.size;
                    }
                    if (stream.is_open()) {
                        stream.close();
                    }
                    if (fileLock.owns_lock()) {
                        if (fileLock.owns_lock()) {
                            fileLock.unlock();
                        }
                    }
                    auto writeEnd = std::chrono::steady_clock::now();
                    writeNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(writeEnd - writeStart).count());
                    
                    if (totalBytes > 0 && blockWritten > 0) {
                        uint64_t current = writtenBytes.fetch_add(blockWritten) + blockWritten;
                        float progress = std::min(0.99f, static_cast<float>(current) / totalBytes);
                        std::lock_guard<std::mutex> lock(progressMutex);
                        console.showInstallationProgress(folderTask.folderName, progress);
                    }
                    
                    return true;
                }));
            }
            
            for (auto& future : futures) {
                if (!future.get()) {
                    return false;
                }
            }
        } else {
            for (size_t i = 0; i < blocks.size(); ++i) {
                const auto& block = blocks[i];
                auto readStart = std::chrono::steady_clock::now();
                std::vector<uint8_t> compressedData = parser.readCompressedData(
                    mapping.offset + block.compressedOffset,
                    block.compressedSize
                );
                auto readEnd = std::chrono::steady_clock::now();
                readNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(readEnd - readStart).count());
                if (compressedData.empty()) {
                    console.showError("Indexed read failed for '" + folderTask.folderName +
                                      "': block " + std::to_string(block.blockId));
                    return false;
                }
                
                auto decompressStart = std::chrono::steady_clock::now();
                std::vector<uint8_t> decompressed;
                if (!decompressor.decompressLzmaBlockData(compressedData, block.originalSize, decompressed)) {
                    console.showError("Indexed decompress failed for '" + folderTask.folderName +
                                      "': block " + std::to_string(block.blockId));
                    return false;
                }
                auto decompressEnd = std::chrono::steady_clock::now();
                decompressNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(decompressEnd - decompressStart).count());
                
                uint64_t blockWritten = 0;
                auto writeStart = std::chrono::steady_clock::now();
                const auto& segs = segments[i];
                size_t currentFileIndex = static_cast<size_t>(-1);
                std::fstream stream;
                std::unique_lock<std::mutex> fileLock;
                for (const auto& seg : segs) {
                    if (seg.fileIndex != currentFileIndex) {
                        if (stream.is_open()) {
                            stream.close();
                        }
                        if (fileLock.owns_lock()) {
                            fileLock.unlock();
                        }
                        currentFileIndex = seg.fileIndex;
                        FileWriter* writer = writerPtrs[currentFileIndex];
                        fileLock = std::unique_lock<std::mutex>(writer->mutex);
                        if (!openFileForWrite(writer->path, stream)) {
                            console.showError("Indexed write failed for '" + folderTask.folderName +
                                              "': block " + std::to_string(block.blockId));
                            return false;
                        }
                    }
                    stream.seekp(static_cast<std::streamoff>(seg.fileOffset));
                    stream.write(reinterpret_cast<const char*>(decompressed.data() + seg.blockOffset),
                                 static_cast<std::streamsize>(seg.size));
                    if (!stream) {
                        console.showError("Indexed write failed for '" + folderTask.folderName +
                                          "': block " + std::to_string(block.blockId));
                        return false;
                    }
                    blockWritten += seg.size;
                }
                if (stream.is_open()) {
                    stream.close();
                }
                if (fileLock.owns_lock()) {
                    fileLock.unlock();
                }
                auto writeEnd = std::chrono::steady_clock::now();
                writeNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(writeEnd - writeStart).count());
                
                if (totalBytes > 0 && blockWritten > 0) {
                    uint64_t current = writtenBytes.fetch_add(blockWritten) + blockWritten;
                    float progress = std::min(0.99f, static_cast<float>(current) / totalBytes);
                    std::lock_guard<std::mutex> lock(progressMutex);
                    console.showInstallationProgress(folderTask.folderName, progress);
                }
            }
        }
        
        console.showInstallationProgress(folderTask.folderName, 1.0f);
        
        auto totalEnd = std::chrono::steady_clock::now();
        timing.totalSec = std::chrono::duration<double>(totalEnd - totalStart).count();
        timing.readSec = static_cast<double>(readNs.load()) / 1e9;
        timing.decompressSec = static_cast<double>(decompressNs.load()) / 1e9;
        timing.writeSec = static_cast<double>(writeNs.load()) / 1e9;
        totalReadNs.fetch_add(readNs.load());
        totalDecompressNs.fetch_add(decompressNs.load());
        totalWriteNs.fetch_add(writeNs.load());
        
        return true;
    };
    
    // 第二阶段：并行执行所有解压任务
    if (!folderTasks.empty()) {
        console.showInfo("Decompressing " + std::to_string(folderTasks.size()) + " folders in parallel...");
        
        std::vector<FolderTask*> indexedTasks;
        std::vector<FolderTask*> regularTasks;
        for (auto& folderTask : folderTasks) {
            if (folderTask.useIndex) {
                indexedTasks.push_back(&folderTask);
            } else {
                regularTasks.push_back(&folderTask);
            }
        }
        
        for (auto* folderTask : regularTasks) {
            threadPool->enqueue([folderTask, &decompressor, &console, &errors, &errorsMutex, 
                                &overallSuccess, &completedFolders, &totalLegacyNs, &folderTimings, &timingMutex,
                                totalFolders = folderTasks.size()]() {
                auto legacyStart = std::chrono::steady_clock::now();
                bool ok = decompressor.decompressFolder(folderTask->decompTask, &folderTask->legacyStage);
                auto legacyEnd = std::chrono::steady_clock::now();
                long long legacyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(legacyEnd - legacyStart).count();
                totalLegacyNs.fetch_add(legacyNs);
                
                if (!ok) {
                    std::string error = "Failed to decompress folder: " + folderTask->folderName;
                    console.showError(error);
                    std::lock_guard<std::mutex> lock(errorsMutex);
                    errors.push_back(error);
                    overallSuccess = false;
                } else {
                    FolderTiming timing;
                    timing.indexed = false;
                    timing.folderName = folderTask->folderName;
                    timing.totalSec = static_cast<double>(legacyNs) / 1e9;
                    timing.readSec = folderTask->legacyReadSec;
                    timing.decompressSec = static_cast<double>(folderTask->legacyStage.decompressNs) / 1e9;
                    timing.writeSec = static_cast<double>(folderTask->legacyStage.writeNs) / 1e9;
                    timing.processSec = std::max(0.0, timing.totalSec - timing.readSec);
                    {
                        std::lock_guard<std::mutex> lock(timingMutex);
                        folderTimings.push_back(timing);
                    }
                    size_t completed = ++completedFolders;
                    float progress = static_cast<float>(completed) / totalFolders;
                    console.showInfo("Progress: " + std::to_string(completed) + "/" + 
                                   std::to_string(totalFolders) + " folders completed (" + 
                                   std::to_string(static_cast<int>(progress * 100)) + "%)");
                }
            });
        }
        
        for (auto* folderTask : indexedTasks) {
            FolderTiming timing;
            bool ok = installWithIndex(*folderTask, timing);
            if (!ok) {
                std::string error = "Failed to decompress folder: " + folderTask->folderName;
                console.showError(error);
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
            } else {
                    {
                        std::lock_guard<std::mutex> lock(timingMutex);
                        folderTimings.push_back(timing);
                    }
                size_t completed = ++completedFolders;
                float progress = static_cast<float>(completed) / folderTasks.size();
                console.showInfo("Progress: " + std::to_string(completed) + "/" + 
                               std::to_string(folderTasks.size()) + " folders completed (" + 
                               std::to_string(static_cast<int>(progress * 100)) + "%)");
            }
        }
    }
    
    // 等待所有任务完成
    threadPool->waitForAll();
    
    // 显示安装结果
    console.showInstallationResult(overallSuccess, errors);
    
    double indexedRead = static_cast<double>(totalReadNs.load()) / 1e9;
    double indexedDecompress = static_cast<double>(totalDecompressNs.load()) / 1e9;
    double indexedWrite = static_cast<double>(totalWriteNs.load()) / 1e9;
    double legacySum = static_cast<double>(totalLegacyNs.load()) / 1e9;
    
    std::cout << "Timing summary: indexed read "
              << std::fixed << std::setprecision(2)
              << indexedRead << "s, indexed decompress "
              << indexedDecompress << "s, indexed write "
              << indexedWrite << "s, legacy total "
              << legacySum << "s" << std::endl;

    for (const auto& timing : folderTimings) {
        if (timing.indexed) {
            std::cout << "Timing (indexed) " << timing.folderName
                      << ": total " << std::fixed << std::setprecision(2) << timing.totalSec
                      << "s, read " << timing.readSec
                      << "s, decompress " << timing.decompressSec
                      << "s, write " << timing.writeSec << "s" << std::endl;
        } else {
            std::cout << "Timing (legacy) " << timing.folderName
                      << ": total " << std::fixed << std::setprecision(2) << timing.totalSec
                      << "s, read " << timing.readSec
                      << "s, decompress " << timing.decompressSec
                      << "s, write " << timing.writeSec
                      << "s, process " << timing.processSec << "s" << std::endl;
        }
    }
    
    if (overallSuccess) {
        if ((metadata.autoStartup || metadata.desktopIcons) && installRootPath.empty()) {
            console.showWarning("Install root not detected; AutoStartup/DesktopIcons skipped");
        }
        
        if (!installRootPath.empty()) {
            std::filesystem::path exePath = findPrimaryExecutable(installRootPath, metadata.applicationName);
            if ((metadata.autoStartup || metadata.desktopIcons) && exePath.empty()) {
                console.showWarning("No executable found for AutoStartup/DesktopIcons");
            } else {
                if (metadata.autoStartup) {
                    if (setAutoStartup(metadata.applicationName, exePath)) {
                        console.showInfo("AutoStartup enabled");
                    } else {
                        console.showWarning("Failed to enable AutoStartup");
                    }
                }
                if (metadata.desktopIcons) {
                    if (createDesktopShortcut(metadata.applicationName, exePath)) {
                        console.showInfo("Desktop icon created");
                    } else {
                        console.showWarning("Failed to create desktop icon");
                    }
                }
            }
        }

        if (!metadata.registry.empty()) {
            applyRegistryEntries(metadata.registry, installRootPath,
                                 metadata.configVersion, metadata.applicationName);
        }
        
        applyInstallState(metadata.installState, "installed", pathResolver);
        if (installMutex) {
            releaseInstallMutex(installMutex);
            installMutex = nullptr;
        }
        console.showInfo("Installation completed successfully!");
        auto endTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        std::cout << "Total time: " << std::fixed << std::setprecision(2) << elapsed.count()
                  << " seconds" << std::endl;
        return 0;
    } else {
        applyInstallState(metadata.installState, "failed", pathResolver);
        if (installMutex) {
            releaseInstallMutex(installMutex);
            installMutex = nullptr;
        }
        console.showError("Installation completed with errors");
        auto endTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        std::cout << "Total time: " << std::fixed << std::setprecision(2) << elapsed.count()
                  << " seconds" << std::endl;
        return 1;
    }
}
