#include "installer/uninstall_manager.h"
#include "installer/file_system_operator.h"
#include "installer/install_state_utils.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

using json = nlohmann::json;

#ifdef _WIN32
static std::string normalizePathForCompare(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    while (!normalized.empty() && (normalized.back() == '\\' || normalized.back() == '/')) {
        normalized.pop_back();
    }
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

static bool isValidUtf8(const std::string& text) {
    size_t i = 0;
    const size_t len = text.size();
    while (i < len) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c <= 0x7F) {
            ++i;
        } else if ((c >> 5) == 0x6) {
            if (i + 1 >= len) return false;
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            if ((c1 >> 6) != 0x2) return false;
            i += 2;
        } else if ((c >> 4) == 0xE) {
            if (i + 2 >= len) return false;
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2) return false;
            i += 3;
        } else if ((c >> 3) == 0x1E) {
            if (i + 3 >= len) return false;
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            unsigned char c3 = static_cast<unsigned char>(text[i + 3]);
            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2 || (c3 >> 6) != 0x2) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

static std::string ansiToUtf8(const std::string& text) {
    if (text.empty()) {
        return text;
    }
    int wideLen = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) {
        return text;
    }
    std::wstring wide(static_cast<size_t>(wideLen - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide.data(), wideLen);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) {
        return text;
    }
    std::string utf8(static_cast<size_t>(utf8Len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
}

static std::string ensureUtf8(const std::string& text) {
    if (text.empty()) {
        return text;
    }
    if (isValidUtf8(text)) {
        return text;
    }
    return ansiToUtf8(text);
}
#else
static std::string ensureUtf8(const std::string& text) {
    return text;
}
#endif

bool writeManifest(const std::string& manifestPath,
                   const std::string& appName,
                   const std::string& configVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& filePaths,
                   const std::vector<RegistryEntry>& registry,
                   bool autoStartup,
                   bool desktopIcons,
                   const InstallStateConfig& installState,
                   const std::string& uninstallPath,
                   const std::string& languageCode) {
    if (manifestPath.empty()) {
        return false;
    }

    try {
        json root;
        root["version"] = "1.0";
        root["appName"] = ensureUtf8(appName);
        root["configVersion"] = ensureUtf8(configVersion);
        root["installDir"] = ensureUtf8(installDir);
        root["uninstallPath"] = ensureUtf8(uninstallPath);

        std::vector<std::string> safeFiles;
        safeFiles.reserve(filePaths.size());
        for (const auto& path : filePaths) {
            safeFiles.push_back(ensureUtf8(path));
        }
        root["files"] = safeFiles;
        root["autoStartup"] = autoStartup;
        root["desktopIcons"] = desktopIcons;
        root["language"] = ensureUtf8(languageCode);

        json reg = json::array();
        for (const auto& entry : registry) {
            json item;
            item["path"] = ensureUtf8(entry.path);
            item["key"] = ensureUtf8(entry.key);
            item["value"] = ensureUtf8(entry.value);
            item["type"] = static_cast<int>(entry.type);
            reg.push_back(item);
        }
        root["registry"] = reg;

        json state;
        state["mode"] = static_cast<int>(installState.mode);
        state["registryPath"] = ensureUtf8(installState.registryPath);
        state["registryKey"] = ensureUtf8(installState.registryKey);
        state["filePath"] = ensureUtf8(installState.filePath);
        state["useMutex"] = installState.useMutex;
        state["mutexName"] = ensureUtf8(installState.mutexName);
        root["installState"] = state;

        std::filesystem::path path(manifestPath);
        std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            FileSystemOperator fs;
            if (!fs.createDirectoryRecursive(toLongPath(parent).string())) {
                return false;
            }
        }

        std::ofstream out(toLongPath(path), std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        std::string payload = root.dump(2, ' ', false, json::error_handler_t::replace);
        out.write(payload.c_str(), static_cast<std::streamsize>(payload.size()));
        return static_cast<bool>(out);
    } catch (const std::exception& e) {
        std::cerr << "Failed to write manifest: " << e.what() << std::endl;
        return false;
    }
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

bool resolveExistingInstallInfo(const std::string& appName,
                                InstallerPathResolver& resolver,
                                std::string& manifestPath,
                                std::string& installDir) {
    manifestPath.clear();
    installDir.clear();
    if (appName.empty()) {
        return false;
    }

    std::string keyName = appName;
    for (char& c : keyName) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }

    const std::string hkcuPath =
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;
    const std::string hklmPath =
        "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;

    std::string legacyPath = "HKEY_CURRENT_USER\\Software\\" + appName;
    std::string legacyInstallDir;
    if (readRegistryStringValue(legacyPath, "InstallDir", legacyInstallDir)) {
        installDir = legacyInstallDir;
        std::filesystem::path localManifest = std::filesystem::path(legacyInstallDir) / "install.manifest.json";
        if (std::filesystem::exists(localManifest)) {
            manifestPath = localManifest.string();
        }
    } else {
        std::string legacyPathHklm = "HKEY_LOCAL_MACHINE\\Software\\" + appName;
        if (readRegistryStringValue(legacyPathHklm, "InstallDir", legacyInstallDir)) {
            installDir = legacyInstallDir;
            std::filesystem::path localManifest = std::filesystem::path(legacyInstallDir) / "install.manifest.json";
            if (std::filesystem::exists(localManifest)) {
                manifestPath = localManifest.string();
            }
        }
    }

    if (!manifestPath.empty() && !installDir.empty()) {
        return true;
    }

    std::string installLocation;
    if (!readRegistryStringValue(hkcuPath, "InstallLocation", installLocation)) {
        readRegistryStringValue(hklmPath, "InstallLocation", installLocation);
    }
    if (!installLocation.empty()) {
        installDir = installLocation;
        std::filesystem::path localManifest = std::filesystem::path(installLocation) / "install.manifest.json";
        if (std::filesystem::exists(localManifest)) {
            manifestPath = localManifest.string();
        }
    }

    if (manifestPath.empty()) {
        std::string uninstallString;
        if (!readRegistryStringValue(hkcuPath, "UninstallString", uninstallString)) {
            readRegistryStringValue(hklmPath, "UninstallString", uninstallString);
        }
        if (!uninstallString.empty()) {
            std::filesystem::path uninstallPath(uninstallString);
            if (std::filesystem::exists(uninstallPath)) {
                std::filesystem::path baseDir = uninstallPath.parent_path();
                if (!baseDir.empty()) {
                    std::filesystem::path localManifest = baseDir / "install.manifest.json";
                    if (std::filesystem::exists(localManifest)) {
                        manifestPath = localManifest.string();
                        if (installDir.empty()) {
                            installDir = baseDir.string();
                        }
                    }
                }
            }
        }
    }

    if (manifestPath.empty()) {
        std::string defaultManifest = getDefaultManifestPath(appName, resolver);
        if (!defaultManifest.empty() && std::filesystem::exists(defaultManifest)) {
            manifestPath = defaultManifest;
        }
    }

    if (!manifestPath.empty()) {
        json manifest;
        if (readManifest(manifestPath, manifest)) {
            std::string manifestInstallDir = manifest.value("installDir", "");
            if (!manifestInstallDir.empty()) {
                installDir = manifestInstallDir;
            }
        }
    }

    return !manifestPath.empty() || !installDir.empty();
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

bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           ConsoleInterface& console) {
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
    bool removedUninstall = false;
    std::string uninstallPath = manifest.value("uninstallPath", "");
    
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

#ifdef _WIN32
    if (!appName.empty()) {
        bool perMachine = isRunningAsAdmin();
        removedUninstall = deleteUninstallRegistryEntry(appName, perMachine);
        if (!removedUninstall) {
            deleteUninstallRegistryEntry(appName, !perMachine);
        }
    }
#endif
    
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
    
    std::string currentExe = getCurrentExecutablePath();
    std::string currentExeNorm = normalizePathForCompare(currentExe);
    std::string uninstallPathNorm = normalizePathForCompare(uninstallPath);
    if (!uninstallPath.empty()) {
        if (!currentExe.empty() && uninstallPathNorm == currentExeNorm) {
            console.showInfo("Skipping uninstall.exe removal (currently running).");
        } else {
            std::filesystem::path path(uninstallPath);
            std::error_code removeEc;
            std::filesystem::remove(toLongPath(path), removeEc);
            if (removeEc && std::filesystem::exists(path)) {
                console.showWarning("Failed to remove uninstall.exe: " + uninstallPath);
            } else if (!removeEc) {
                console.showInfo("Removed uninstall.exe: " + uninstallPath);
            }
        }
    }

    for (const auto& file : files) {
        std::string fileNorm = normalizePathForCompare(file);
        if (!currentExe.empty() && fileNorm == currentExeNorm) {
            console.showInfo("Skipping file removal (current exe): " + file);
            continue;
        }
        std::filesystem::path path(file);
        std::error_code removeEc;
        std::filesystem::remove(toLongPath(path), removeEc);
        if (removeEc && std::filesystem::exists(path)) {
            console.showWarning("Failed to remove file: " + file);
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
        std::vector<std::filesystem::path> emptyDirs;
        std::filesystem::path rootPath(root);
        if (!std::filesystem::exists(rootPath)) {
            continue;
        }
        auto cleanupStart = std::chrono::steady_clock::now();
        console.showInfo("Cleanup dirs start: " + root);
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
        auto cleanupEnd = std::chrono::steady_clock::now();
        auto cleanupMs = std::chrono::duration_cast<std::chrono::milliseconds>(cleanupEnd - cleanupStart).count();
        console.showInfo("Cleanup dirs done: " + root + " (" + std::to_string(cleanupMs) + " ms)");
        
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

} // namespace MultiThreadedInstaller
