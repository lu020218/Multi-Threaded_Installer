#include "installer/uninstall_manager.h"
#include "installer/file_system_operator.h"
#include "installer/install_state_utils.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"
#include "common/utf8_utils.h"
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

static std::string ensureUtf8(const std::string& text) {
    if (text.empty()) {
        return text;
    }
    if (isValidUtf8(text)) {
        return text;
    }
    std::string utf8 = AcpToUtf8(text);
    return utf8.empty() ? text : utf8;
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
                   const std::vector<std::string>& installKillProcesses,
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
        std::vector<std::string> safeInstallKill;
        safeInstallKill.reserve(installKillProcesses.size());
        for (const auto& name : installKillProcesses) {
            safeInstallKill.push_back(ensureUtf8(name));
        }
        root["killProcesses"] = safeInstallKill;

        json state;
        state["mode"] = static_cast<int>(installState.mode);
        state["registryPath"] = ensureUtf8(installState.registryPath);
        state["registryKey"] = ensureUtf8(installState.registryKey);
        state["filePath"] = ensureUtf8(installState.filePath);
        state["useMutex"] = installState.useMutex;
        state["mutexName"] = ensureUtf8(installState.mutexName);
        root["installState"] = state;

        std::filesystem::path path = PathFromUtf8(manifestPath);
        std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            FileSystemOperator fs;
            if (!fs.createDirectoryRecursive(Utf8FromPath(parent))) {
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
    std::ifstream in(toLongPath(PathFromUtf8(manifestPath)), std::ios::binary);
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
        std::filesystem::path localManifest = PathFromUtf8(legacyInstallDir) / "install.manifest.json";
        if (std::filesystem::exists(localManifest)) {
            manifestPath = Utf8FromPath(localManifest);
        }
    } else {
        std::string legacyPathHklm = "HKEY_LOCAL_MACHINE\\Software\\" + appName;
        if (readRegistryStringValue(legacyPathHklm, "InstallDir", legacyInstallDir)) {
            installDir = legacyInstallDir;
            std::filesystem::path localManifest = PathFromUtf8(legacyInstallDir) / "install.manifest.json";
            if (std::filesystem::exists(localManifest)) {
                manifestPath = Utf8FromPath(localManifest);
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
        std::filesystem::path localManifest = PathFromUtf8(installLocation) / "install.manifest.json";
        if (std::filesystem::exists(localManifest)) {
            manifestPath = Utf8FromPath(localManifest);
        }
    }

    if (manifestPath.empty()) {
        std::string uninstallString;
        if (!readRegistryStringValue(hkcuPath, "UninstallString", uninstallString)) {
            readRegistryStringValue(hklmPath, "UninstallString", uninstallString);
        }
        if (!uninstallString.empty()) {
            std::filesystem::path uninstallPath = PathFromUtf8(uninstallString);
            if (std::filesystem::exists(uninstallPath)) {
                std::filesystem::path baseDir = uninstallPath.parent_path();
                if (!baseDir.empty()) {
                    std::filesystem::path localManifest = baseDir / "install.manifest.json";
                    if (std::filesystem::exists(localManifest)) {
                        manifestPath = Utf8FromPath(localManifest);
                        if (installDir.empty()) {
                            installDir = Utf8FromPath(baseDir);
                        }
                    }
                }
            }
        }
    }

    if (manifestPath.empty()) {
        std::string defaultManifest = getDefaultManifestPath(appName, resolver);
        if (!defaultManifest.empty() && std::filesystem::exists(PathFromUtf8(defaultManifest))) {
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
    std::wstring wide = Utf8ToWide(exePath);
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

    std::wstring exePathW = Utf8ToWide(exePath);
    if (exePathW.empty()) {
        return false;
    }

    wchar_t tempPath[MAX_PATH] = {};
    DWORD len = GetTempPathW(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) {
        return false;
    }

    wchar_t tempFile[MAX_PATH] = {};
    if (GetTempFileNameW(tempPath, L"un", 0, tempFile) == 0) {
        return false;
    }

    std::wstring scriptPath = std::wstring(tempFile) + L".cmd";
    std::filesystem::path scriptFs(scriptPath);
    std::ofstream script(scriptFs, std::ios::binary | std::ios::trunc);
    if (!script) {
        return false;
    }

    auto writeLine = [&](const std::wstring& line) {
        script.write(reinterpret_cast<const char*>(line.c_str()),
                     static_cast<std::streamsize>(line.size() * sizeof(wchar_t)));
    };

    const unsigned char bom[] = {0xFF, 0xFE};
    script.write(reinterpret_cast<const char*>(bom), sizeof(bom));

    writeLine(L"@echo off\r\n");
    writeLine(L":repeat\r\n");
    writeLine(L"del /f /q \"" + exePathW + L"\" >nul 2>&1\r\n");
    writeLine(L"if exist \"" + exePathW + L"\" (\r\n");
    writeLine(L"  ping 127.0.0.1 -n 2 >nul\r\n");
    writeLine(L"  goto repeat\r\n");
    writeLine(L")\r\n");

    if (!manifestPath.empty()) {
        std::wstring manifestW = Utf8ToWide(manifestPath);
        if (!manifestW.empty()) {
            writeLine(L"if exist \"" + manifestW + L"\" del /f /q \"" + manifestW + L"\" >nul 2>&1\r\n");
        }
    }
    for (const auto& root : cleanupRoots) {
        if (root.empty()) {
            continue;
        }
        std::wstring rootW = Utf8ToWide(root);
        if (rootW.empty()) {
            continue;
        }
        writeLine(L"if exist \"" + rootW + L"\" (\r\n");
        writeLine(L"  for /f \"delims=\" %%d in ('dir /ad /b /s \"" + rootW + L"\" ^| sort /r') do rmdir \"%%d\" 2>nul\r\n");
        writeLine(L"  rmdir \"" + rootW + L"\" 2>nul\r\n");
        writeLine(L")\r\n");
    }
    writeLine(L"del /f /q \"%~f0\" >nul 2>&1\r\n");
    script.close();

    std::wstring cmd = L"cmd.exe /c start \"\" /b \"" + scriptPath + L"\"";
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');
    BOOL ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
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
    std::wstring rootW = Utf8ToWide(root);
    if (rootW.empty()) {
        return false;
    }

    std::wstring cmd = L"cmd.exe /c \"";
    cmd += L"del /f /q /a \"" + rootW + L"\\\\desktop.ini\" /s >nul 2>&1 & ";
    cmd += L"del /f /q /a \"" + rootW + L"\\\\thumbs.db\" /s >nul 2>&1 & ";
    cmd += L"for /f \\\"delims=\\\" %%d in ('dir /ad /b /s \\\"" + rootW + L"\\\" ^| sort /r') do rmdir \\\"%%d\\\" 2>nul & ";
    cmd += L"rmdir \\\"" + rootW + L"\\\" 2>nul\"";

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');
    BOOL ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
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
    std::vector<std::string> installKillProcesses;
    if (manifest.contains("killProcesses")) {
        const auto& kill = manifest["killProcesses"];
        if (kill.is_array()) {
            for (const auto& item : kill) {
                if (item.is_string()) {
                    installKillProcesses.push_back(item.get<std::string>());
                }
            }
        } else if (kill.is_string()) {
            installKillProcesses.push_back(kill.get<std::string>());
        }
    }

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
    
    std::vector<std::string> killTargets = buildKillProcessList(appName, installKillProcesses);
    if (!killTargets.empty()) {
        std::vector<std::string> running = getRunningProcessesByName(killTargets);
        if (!running.empty()) {
            auto joinNames = [](const std::vector<std::string>& names) {
                std::string joined;
                for (size_t i = 0; i < names.size(); ++i) {
                    if (i > 0) {
                        joined += ", ";
                    }
                    joined += names[i];
                }
                return joined;
            };
            console.showInfo("Terminating processes: " + joinNames(running));
            terminateProcessesByName(running);
#ifdef _WIN32
            Sleep(500);
#endif
            std::vector<std::string> remaining = getRunningProcessesByName(killTargets);
            if (!remaining.empty()) {
                console.showWarning("Some processes are still running: " + joinNames(remaining));
            }
        }
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
            std::filesystem::path path = PathFromUtf8(file);
            for (const auto& part : path) {
                std::string partStr = Utf8FromPath(part);
                std::string partLower = partStr;
                std::transform(partLower.begin(), partLower.end(), partLower.begin(), ::tolower);
                if (partLower == appLower) {
                    std::filesystem::path root;
                    for (const auto& build : path) {
                        root /= build;
                        if (build == part) {
                            cleanupRoots.push_back(Utf8FromPath(root));
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
            std::filesystem::path path = PathFromUtf8(uninstallPath);
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
        std::filesystem::path path = PathFromUtf8(file);
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
            std::string name = Utf8FromPath(entry.path().filename());
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
        std::filesystem::path rootPath = PathFromUtf8(root);
        if (!std::filesystem::exists(rootPath)) {
            continue;
        }
        auto cleanupStart = std::chrono::steady_clock::now();
        console.showInfo("Cleanup dirs start: " + root);
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(toLongPath(rootPath), options)) {
            if (entry.is_regular_file()) {
                std::string name = Utf8FromPath(entry.path().filename());
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
                console.showWarning("Failed to remove empty directory: " + Utf8FromPath(dir));
            }
        }
        std::error_code ec;
        std::filesystem::remove(toLongPath(rootPath), ec);
        if (ec && std::filesystem::exists(rootPath)) {
            console.showWarning("Failed to remove root directory: " + Utf8FromPath(rootPath));
        }
        auto cleanupEnd = std::chrono::steady_clock::now();
        auto cleanupMs = std::chrono::duration_cast<std::chrono::milliseconds>(cleanupEnd - cleanupStart).count();
        console.showInfo("Cleanup dirs done: " + root + " (" + std::to_string(cleanupMs) + " ms)");
        
        if (!hasNonIgnoredFiles(rootPath)) {
            std::error_code removeEc;
            std::filesystem::remove_all(toLongPath(rootPath), removeEc);
            if (removeEc && std::filesystem::exists(rootPath)) {
                console.showWarning("Failed to remove empty root tree: " + Utf8FromPath(rootPath));
            } else if (!removeEc) {
                console.showInfo("Removed empty root tree: " + Utf8FromPath(rootPath));
            }
        } else {
            console.showWarning("Root not empty after cleanup: " + Utf8FromPath(rootPath));
        }
    }
    
    removeInstallStateArtifacts(installState, resolver);
    applyInstallState(installState, "uninstalled", resolver);
    if (!std::filesystem::remove(toLongPath(PathFromUtf8(manifestPath)))) {
        if (std::filesystem::exists(PathFromUtf8(manifestPath))) {
            console.showWarning("Failed to remove manifest: " + manifestPath);
        }
    }
    if (!appName.empty()) {
        std::string defaultPath = getDefaultManifestPath(appName, resolver);
        if (!defaultPath.empty() && defaultPath != manifestPath) {
            std::filesystem::remove(toLongPath(PathFromUtf8(defaultPath)));
        }
    }
    
    std::filesystem::path exePath = PathFromUtf8(getCurrentExecutablePath());
    std::string exeName = Utf8FromPath(exePath.filename());
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
