#include "installer/metadata_parser.h"
#include "installer/thread_pool_manager.h"
#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include "installer/console_interface.h"
#include "installer/path_resolver.h"
#include <json.hpp>
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
#else
#include <unistd.h>
#endif

using namespace MultiThreadedInstaller;
using json = nlohmann::json;

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

void applyInstallState(const InstallStateConfig& config, const std::string& stateValue,
                       InstallerPathResolver& resolver);

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

std::string getCurrentExecutablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0) {
        return "";
    }
    return std::string(buffer, len);
#else
    char buffer[1024];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        return "";
    }
    buffer[len] = '\0';
    return std::string(buffer);
#endif
}

std::string getDefaultManifestPath(const std::string& appName, InstallerPathResolver& resolver) {
    std::string base = "%ProgramData%\\" + appName;
    std::string expanded = resolver.expandEnvironmentVariables(base);
    if (expanded.empty()) {
        return "";
    }
    std::filesystem::path path(expanded);
    path /= "install.manifest.json";
    return path.string();
}

std::string getLocalManifestPath(const std::string& exePath) {
    if (exePath.empty()) {
        return "";
    }
    std::filesystem::path path(exePath);
    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return "";
    }
    parent /= "install.manifest.json";
    return parent.string();
}

bool createUninstallStub(const std::string& sourcePath, const std::string& targetPath) {
    struct DataLocator {
        uint32_t magic;
        uint64_t metadataOffset;
        uint64_t metadataSize;
        uint64_t dataOffset;
        uint64_t dataSize;
    };
    
    std::ifstream in(toLongPath(std::filesystem::path(sourcePath)), std::ios::binary);
    if (!in) {
        return false;
    }
    
    in.seekg(0, std::ios::end);
    std::streampos fileSize = in.tellg();
    size_t locatorSize = sizeof(DataLocator) + sizeof(uint32_t);
    if (fileSize < static_cast<std::streampos>(locatorSize)) {
        return false;
    }
    
    in.seekg(-static_cast<std::streamoff>(sizeof(uint32_t)), std::ios::end);
    uint32_t endMagic = 0;
    in.read(reinterpret_cast<char*>(&endMagic), sizeof(uint32_t));
    if (endMagic != Constants::MAGIC_NUMBER) {
        return false;
    }
    
    in.seekg(-static_cast<std::streamoff>(locatorSize), std::ios::end);
    DataLocator locator{};
    in.read(reinterpret_cast<char*>(&locator), sizeof(DataLocator));
    if (locator.magic != Constants::MAGIC_NUMBER || locator.metadataOffset == 0) {
        return false;
    }
    
    if (locator.metadataOffset >= static_cast<uint64_t>(fileSize)) {
        return false;
    }
    
    std::ofstream out(toLongPath(std::filesystem::path(targetPath)), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    
    in.seekg(0, std::ios::beg);
    const size_t bufSize = 1024 * 1024;
    std::vector<char> buffer(bufSize);
    uint64_t remaining = locator.metadataOffset;
    while (remaining > 0) {
        size_t chunk = remaining > bufSize ? bufSize : static_cast<size_t>(remaining);
        in.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!in) {
            return false;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!out) {
            return false;
        }
        remaining -= chunk;
    }
    return true;
}

bool writeManifest(const std::string& manifestPath,
                   const std::string& appName,
                   const std::string& configVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& filePaths,
                   const std::vector<RegistryEntry>& registry,
                   bool autoStartup,
                   bool desktopIcons,
                   const InstallStateConfig& installState,
                   const std::string& uninstallPath) {
    if (manifestPath.empty()) {
        return false;
    }
    
    json root;
    root["version"] = "1.0";
    root["appName"] = appName;
    root["configVersion"] = configVersion;
    root["installDir"] = installDir;
    root["uninstallPath"] = uninstallPath;
    root["files"] = filePaths;
    root["autoStartup"] = autoStartup;
    root["desktopIcons"] = desktopIcons;
    
    json reg = json::array();
    for (const auto& entry : registry) {
        json item;
        item["path"] = entry.path;
        item["key"] = entry.key;
        item["value"] = entry.value;
        item["type"] = static_cast<int>(entry.type);
        reg.push_back(item);
    }
    root["registry"] = reg;
    
    json state;
    state["mode"] = static_cast<int>(installState.mode);
    state["registryPath"] = installState.registryPath;
    state["registryKey"] = installState.registryKey;
    state["filePath"] = installState.filePath;
    state["useMutex"] = installState.useMutex;
    state["mutexName"] = installState.mutexName;
    root["installState"] = state;
    
    std::filesystem::path path(manifestPath);
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        FileSystemOperator fs;
        if (!fs.createDirectoryRecursive(parent.string())) {
            return false;
        }
    }
    
    std::ofstream out(toLongPath(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    std::string payload = root.dump(2);
    out.write(payload.c_str(), static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(out);
}

bool readManifest(const std::string& manifestPath, json& outManifest) {
    if (manifestPath.empty()) {
        return false;
    }
    std::ifstream in(toLongPath(std::filesystem::path(manifestPath)), std::ios::binary);
    if (!in) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty()) {
        return false;
    }
    outManifest = json::parse(content, nullptr, false);
    return !outManifest.is_discarded();
}

bool deleteRegistryValue(const RegistryEntry& entry) {
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
    LONG status = RegOpenKeyExA(root, subkey.c_str(), 0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    
    status = RegDeleteValueA(key, entry.key.c_str());
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)entry;
    return false;
#endif
}

bool removeAutoStartup(const std::string& appName) {
#ifdef _WIN32
    HKEY key = nullptr;
    LONG status = RegOpenKeyExA(HKEY_CURRENT_USER,
                                "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    status = RegDeleteValueA(key, appName.c_str());
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)appName;
    return false;
#endif
}

bool deleteDesktopShortcut(const std::string& appName) {
#ifdef _WIN32
    PWSTR desktopPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktopPath);
    if (FAILED(hr) || !desktopPath) {
        return false;
    }
    std::wstring linkPath = std::wstring(desktopPath) + L"\\" + toWideUtf8(appName) + L".lnk";
    CoTaskMemFree(desktopPath);
    return DeleteFileW(linkPath.c_str()) != 0;
#else
    (void)appName;
    return false;
#endif
}

bool scheduleSelfDelete() {
#ifdef _WIN32
    std::string exePath = getCurrentExecutablePath();
    if (exePath.empty()) {
        return false;
    }
    std::wstring wide = toWideUtf8(exePath);
    if (wide.empty()) {
        return false;
    }
    return MoveFileExW(wide.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != 0;
#else
    return false;
#endif
}

bool scheduleSelfDeleteImmediate(const std::vector<std::string>& cleanupRoots,
                                 const std::string& manifestPath) {
#ifdef _WIN32
    std::string exePath = getCurrentExecutablePath();
    if (exePath.empty()) {
        return false;
    }
    
    char tempPath[MAX_PATH] = {0};
    DWORD len = GetTempPathA(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) {
        return false;
    }
    
    char tempFile[MAX_PATH] = {0};
    if (GetTempFileNameA(tempPath, "un", 0, tempFile) == 0) {
        return false;
    }
    
    std::string scriptPath = std::string(tempFile) + ".cmd";
    std::ofstream script(scriptPath, std::ios::binary | std::ios::trunc);
    if (!script) {
        return false;
    }
    
    script << "@echo off\n";
    script << ":repeat\n";
    script << "del /f /q \"" << exePath << "\" >nul 2>&1\n";
    script << "if exist \"" << exePath << "\" (\n";
    script << "  ping 127.0.0.1 -n 2 >nul\n";
    script << "  goto repeat\n";
    script << ")\n";
    if (!manifestPath.empty()) {
        script << "if exist \"" << manifestPath << "\" del /f /q \"" << manifestPath << "\" >nul 2>&1\n";
    }
    for (const auto& root : cleanupRoots) {
        if (root.empty()) {
            continue;
        }
        script << "if exist \"" << root << "\" (\n";
        script << "  for /f \"delims=\" %%d in ('dir /ad /b /s \"" << root << "\" ^| sort /r') do rmdir \"%%d\" 2>nul\n";
        script << "  rmdir \"" << root << "\" 2>nul\n";
        script << ")\n";
    }
    script << "del /f /q \"%~f0\" >nul 2>&1\n";
    script.close();
    
    std::string cmd = "cmd.exe /c start \"\" /b \"" + scriptPath + "\"";
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ok == TRUE;
#else
    return false;
#endif
}

bool cleanupEmptyDirectoriesCmd(const std::string& root) {
#ifdef _WIN32
    if (root.empty()) {
        return false;
    }
    
    std::string cmd = "cmd.exe /c \"";
    cmd += "del /f /q /a \"" + root + "\\\\desktop.ini\" /s >nul 2>&1 & ";
    cmd += "del /f /q /a \"" + root + "\\\\thumbs.db\" /s >nul 2>&1 & ";
    cmd += "for /f \\\"delims=\\\" %%d in ('dir /ad /b /s \\\"" + root + "\\\" ^| sort /r') do rmdir \\\"%%d\\\" 2>nul & ";
    cmd += "rmdir \\\"" + root + "\\\" 2>nul\"";
    
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) {
        return false;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, 30000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    (void)root;
    return false;
#endif
}

bool removeInstallStateArtifacts(const InstallStateConfig& config, InstallerPathResolver& resolver) {
    bool ok = true;
    if (config.mode == InstallStateMode::REGISTRY || config.mode == InstallStateMode::BOTH) {
        RegistryEntry entry;
        entry.path = config.registryPath;
        entry.key = config.registryKey.empty() ? "InstallState" : config.registryKey;
        ok = deleteRegistryValue(entry) && ok;
    }
    if (config.mode == InstallStateMode::FILE || config.mode == InstallStateMode::BOTH) {
        std::string expanded = resolver.expandEnvironmentVariables(config.filePath);
        if (!expanded.empty()) {
            std::filesystem::remove(toLongPath(std::filesystem::path(expanded)));
        }
    }
    return ok;
}

bool uninstallFromManifest(const std::string& manifestPath, InstallerPathResolver& resolver, ConsoleInterface& console) {
    json manifest;
    if (!readManifest(manifestPath, manifest)) {
        console.showError("Failed to read manifest: " + manifestPath);
        return false;
    }
    console.showInfo("Loaded manifest: " + manifestPath);
    
    std::string appName = manifest.value("appName", "");
    std::string installDir = manifest.value("installDir", "");
    bool autoStartup = manifest.value("autoStartup", false);
    bool desktopIcons = manifest.value("desktopIcons", false);
    
    InstallStateConfig installState;
    if (manifest.contains("installState")) {
        const auto& state = manifest["installState"];
        installState.mode = static_cast<InstallStateMode>(state.value("mode", 0));
        installState.registryPath = state.value("registryPath", "");
        installState.registryKey = state.value("registryKey", "");
        installState.filePath = state.value("filePath", "");
        installState.useMutex = state.value("useMutex", true);
        installState.mutexName = state.value("mutexName", "");
    }
    
    applyInstallState(installState, "uninstalling", resolver);
    
    if (autoStartup && !appName.empty()) {
        removeAutoStartup(appName);
    }
    if (desktopIcons && !appName.empty()) {
        deleteDesktopShortcut(appName);
    }
    
    if (manifest.contains("registry") && manifest["registry"].is_array()) {
        for (const auto& reg : manifest["registry"]) {
            RegistryEntry entry;
            entry.path = reg.value("path", "");
            entry.key = reg.value("key", "");
            deleteRegistryValue(entry);
        }
    }
    
    std::vector<std::string> files;
    if (manifest.contains("files") && manifest["files"].is_array()) {
        for (const auto& item : manifest["files"]) {
            if (item.is_string()) {
                files.push_back(item.get<std::string>());
            }
        }
    }
    console.showInfo("Manifest files: " + std::to_string(files.size()));
    
    std::vector<std::string> cleanupRoots;
    if (!appName.empty()) {
        std::string appLower = appName;
        std::transform(appLower.begin(), appLower.end(), appLower.begin(), ::tolower);
        for (const auto& file : files) {
            std::filesystem::path path(file);
            for (const auto& part : path) {
                std::string partStr = part.string();
                std::string partLower = partStr;
                std::transform(partLower.begin(), partLower.end(), partLower.begin(), ::tolower);
                if (partLower == appLower) {
                    std::filesystem::path root;
                    for (const auto& build : path) {
                        root /= build;
                        if (build == part) {
                            cleanupRoots.push_back(root.string());
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }
    if (!installDir.empty()) {
        cleanupRoots.push_back(installDir);
    }
    std::sort(cleanupRoots.begin(), cleanupRoots.end());
    cleanupRoots.erase(std::unique(cleanupRoots.begin(), cleanupRoots.end()), cleanupRoots.end());
    console.showInfo("Cleanup roots: " + std::to_string(cleanupRoots.size()));
    for (const auto& root : cleanupRoots) {
        console.showInfo("Cleanup root: " + root);
    }
    
    for (const auto& file : files) {
        std::filesystem::path path(file);
        if (!std::filesystem::remove(toLongPath(path))) {
            if (std::filesystem::exists(path)) {
                console.showWarning("Failed to remove file: " + file);
            }
        }
    }
    
    auto hasNonIgnoredFiles = [](const std::filesystem::path& rootPath) -> bool {
        if (!std::filesystem::exists(rootPath)) {
            return false;
        }
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(toLongPath(rootPath), options)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name == "desktop.ini" || name == "thumbs.db") {
                continue;
            }
            return true;
        }
        return false;
    };
    
    for (const auto& root : cleanupRoots) {
        console.showInfo("Cleanup cmd start: " + root);
        if (!cleanupEmptyDirectoriesCmd(root)) {
            console.showWarning("Cleanup cmd failed or timed out: " + root);
        } else {
            console.showInfo("Cleanup cmd done: " + root);
        }
        std::vector<std::filesystem::path> emptyDirs;
        std::filesystem::path rootPath(root);
        if (!std::filesystem::exists(rootPath)) {
            continue;
        }
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(toLongPath(rootPath), options)) {
            if (entry.is_regular_file()) {
                std::string name = entry.path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "desktop.ini" || name == "thumbs.db") {
                    std::error_code ec;
                    std::filesystem::remove(toLongPath(entry.path()), ec);
                }
            }
            if (entry.is_directory()) {
                emptyDirs.push_back(entry.path());
            }
        }
        std::sort(emptyDirs.begin(), emptyDirs.end(), [](const auto& a, const auto& b) {
            return a.native().size() > b.native().size();
        });
        for (const auto& dir : emptyDirs) {
            std::error_code ec;
            std::filesystem::remove(toLongPath(dir), ec);
            if (ec && std::filesystem::exists(dir)) {
                console.showWarning("Failed to remove empty directory: " + dir.string());
            }
        }
        std::error_code ec;
        std::filesystem::remove(toLongPath(rootPath), ec);
        if (ec && std::filesystem::exists(rootPath)) {
            console.showWarning("Failed to remove root directory: " + rootPath.string());
        }
        
        if (!hasNonIgnoredFiles(rootPath)) {
            std::error_code removeEc;
            std::filesystem::remove_all(toLongPath(rootPath), removeEc);
            if (removeEc && std::filesystem::exists(rootPath)) {
                console.showWarning("Failed to remove empty root tree: " + rootPath.string());
            } else if (!removeEc) {
                console.showInfo("Removed empty root tree: " + rootPath.string());
            }
        } else {
            console.showWarning("Root not empty after cleanup: " + rootPath.string());
        }
    }
    
    removeInstallStateArtifacts(installState, resolver);
    applyInstallState(installState, "uninstalled", resolver);
    if (!std::filesystem::remove(toLongPath(std::filesystem::path(manifestPath)))) {
        if (std::filesystem::exists(manifestPath)) {
            console.showWarning("Failed to remove manifest: " + manifestPath);
        }
    }
    if (!appName.empty()) {
        std::string defaultPath = getDefaultManifestPath(appName, resolver);
        if (!defaultPath.empty() && defaultPath != manifestPath) {
            std::filesystem::remove(toLongPath(std::filesystem::path(defaultPath)));
        }
    }
    
    std::filesystem::path exePath(getCurrentExecutablePath());
    std::string exeName = exePath.filename().string();
    std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
    if (exeName == "uninstall.exe") {
        if (!scheduleSelfDeleteImmediate(cleanupRoots, manifestPath)) {
            if (!scheduleSelfDelete()) {
                console.showWarning("Failed to schedule uninstall.exe removal");
            }
        } else {
            console.showInfo("Scheduled immediate uninstall.exe removal");
        }
    }
    console.showInfo("Uninstall completed");
    return true;
}

std::vector<std::string> collectFilesRecursive(const std::string& rootPath) {
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
    if (!args.uninstall) {
        std::filesystem::path exePath = getCurrentExecutablePath();
        std::string exeName = exePath.filename().string();
        std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
        if (exeName == "uninstall.exe") {
            args.uninstall = true;
        }
    }
    
    if (args.showHelp) {
        console.showInstallerHelp();
        return 0;
    }
    
    if (args.uninstall) {
        console.showInfo("Starting uninstall process...");
        InstallerPathResolver pathResolver;
        std::string exePath = getCurrentExecutablePath();
        std::string localManifest = getLocalManifestPath(exePath);
        if (!localManifest.empty() && std::filesystem::exists(localManifest)) {
            bool ok = uninstallFromManifest(localManifest, pathResolver, console);
            return ok ? 0 : 1;
        }
        std::string fallbackAppName;
        if (!exePath.empty()) {
            std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
            if (!exeDir.empty()) {
                fallbackAppName = exeDir.filename().string();
            }
        }
        if (fallbackAppName.empty()) {
            std::filesystem::path exeName = std::filesystem::path(exePath).filename();
            fallbackAppName = exeName.stem().string();
        }
        if (!fallbackAppName.empty()) {
            std::string manifestPath = getDefaultManifestPath(fallbackAppName, pathResolver);
            if (!manifestPath.empty() && std::filesystem::exists(manifestPath)) {
                bool ok = uninstallFromManifest(manifestPath, pathResolver, console);
                return ok ? 0 : 1;
            }
        }
        MetadataParser parser;
        auto metadata = parser.parseExtendedEmbeddedMetadata();
        if (parser.validateMetadata(metadata)) {
            std::string manifestPath = getDefaultManifestPath(metadata.applicationName, pathResolver);
            if (!manifestPath.empty() && std::filesystem::exists(manifestPath)) {
                bool ok = uninstallFromManifest(manifestPath, pathResolver, console);
                return ok ? 0 : 1;
            }
        }
        console.showError("Manifest not found for uninstall");
        return 1;
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

        std::vector<std::string> installedFiles;
        for (const auto& folderTask : folderTasks) {
            auto files = collectFilesRecursive(folderTask.targetPath);
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
                    if (ec) {
                        console.showWarning("Failed to create uninstall.exe");
                    } else {
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
        if (!writeManifest(manifestPath, metadata.applicationName, metadata.configVersion,
                           installRootPath, installedFiles, metadata.registry,
                           metadata.autoStartup, metadata.desktopIcons,
                           metadata.installState, uninstallPath)) {
            console.showWarning("Failed to write install manifest");
        }
        
        if (!installRootPath.empty()) {
            std::filesystem::path localPath = std::filesystem::path(installRootPath) / "install.manifest.json";
            if (!writeManifest(localPath.string(), metadata.applicationName, metadata.configVersion,
                               installRootPath, installedFiles, metadata.registry,
                               metadata.autoStartup, metadata.desktopIcons,
                               metadata.installState, uninstallPath)) {
                console.showWarning("Failed to write local install manifest");
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
