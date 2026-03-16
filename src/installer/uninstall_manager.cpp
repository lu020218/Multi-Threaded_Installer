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
#include <limits>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

using json = nlohmann::json;

#ifdef _WIN32
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

static std::vector<std::string> EnsureUtf8List(const std::vector<std::string>& values) {
    std::vector<std::string> safeValues;
    safeValues.reserve(values.size());
    for (const auto& value : values) {
        safeValues.push_back(ensureUtf8(value));
    }
    return safeValues;
}

static std::string GetManifestAppId(const json& manifest) {
    std::string appId = manifest.value("appId", "");
    if (!appId.empty()) {
        return appId;
    }
    return manifest.value("appName", "");
}

static std::string GetManifestDisplayName(const json& manifest) {
    std::string displayName = manifest.value("displayName", "");
    if (!displayName.empty()) {
        return displayName;
    }
    return manifest.value("appName", "");
}

static std::vector<std::string> GetManifestLegacyAppIds(const json& manifest) {
    std::vector<std::string> legacyAppIds;
    if (manifest.contains("legacyAppIds") && manifest["legacyAppIds"].is_array()) {
        for (const auto& item : manifest["legacyAppIds"]) {
            if (item.is_string()) {
                legacyAppIds.push_back(item.get<std::string>());
            }
        }
    }
    return legacyAppIds;
}

static bool ExistingInstallDirectoryLooksValid(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(PathFromUtf8(path), ec) &&
           std::filesystem::is_directory(PathFromUtf8(path), ec);
}

bool writeManifest(const std::string& manifestPath,
                   const std::string& appId,
                   const std::string& displayName,
                   const std::vector<std::string>& legacyAppIds,
                   const std::string& configVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& filePaths,
                   const std::vector<RegistryEntry>& registry,
                   const std::vector<std::string>& installKillProcesses,
                   bool autoStartup,
                   bool desktopIcons,
                   const InstallStateConfig& installState,
                   const std::string& uninstallPath,
                   const std::string& languageCode,
                   const std::vector<ComponentExecutionRecord>& componentActions) {
    if (manifestPath.empty()) {
        return false;
    }

    try {
        json root;
        root["version"] = "1.0";
        root["appId"] = ensureUtf8(appId);
        root["displayName"] = ensureUtf8(displayName);
        root["legacyAppIds"] = EnsureUtf8List(legacyAppIds);
        root["appName"] = ensureUtf8(displayName);
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
        json actions = json::array();
        for (const auto& action : componentActions) {
            if (action.uninstallCommand.empty()) {
                continue;
            }
            json item;
            item["componentId"] = ensureUtf8(action.componentId);
            item["sourceType"] = ensureUtf8(action.sourceType);
            item["uninstallCommand"] = ensureUtf8(action.uninstallCommand);
            item["workingDirectory"] = ensureUtf8(action.workingDirectory);
            item["wait"] = action.wait;
            item["timeoutSec"] = action.timeoutSec;
            actions.push_back(item);
        }
        root["componentActions"] = actions;

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

bool resolveExistingInstallInfo(const std::vector<std::string>& identityCandidates,
                                InstallerPathResolver& resolver,
                                std::string& manifestPath,
                                std::string& installDir,
                                std::string* matchedIdentity) {
    manifestPath.clear();
    installDir.clear();
    if (matchedIdentity) {
        matchedIdentity->clear();
    }
    if (identityCandidates.empty()) {
        return false;
    }

    for (const auto& identity : identityCandidates) {
        if (identity.empty()) {
            continue;
        }

        std::string candidateManifest;
        std::string candidateInstallDir;
        std::string keyName = sanitizeRegistryKeyName(identity);

        const std::string hkcuPath =
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;
        const std::string hklmPath =
            "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;

        std::string legacyPath = "HKEY_CURRENT_USER\\Software\\" + identity;
        std::string legacyInstallDir;
        if (!readRegistryStringValue(legacyPath, "InstallDir", legacyInstallDir)) {
            std::string legacyPathHklm = "HKEY_LOCAL_MACHINE\\Software\\" + identity;
            readRegistryStringValue(legacyPathHklm, "InstallDir", legacyInstallDir);
        }
        if (!legacyInstallDir.empty()) {
            if (ExistingInstallDirectoryLooksValid(legacyInstallDir)) {
                candidateInstallDir = legacyInstallDir;
            }
            std::filesystem::path localManifest = PathFromUtf8(legacyInstallDir) / "install.manifest.json";
            if (std::filesystem::exists(localManifest)) {
                candidateManifest = Utf8FromPath(localManifest);
                if (candidateInstallDir.empty()) {
                    candidateInstallDir = legacyInstallDir;
                }
            }
        }

        if (candidateManifest.empty()) {
            std::string installLocation;
            if (!readRegistryStringValue(hkcuPath, "InstallLocation", installLocation)) {
                readRegistryStringValue(hklmPath, "InstallLocation", installLocation);
            }
            if (!installLocation.empty()) {
                if (ExistingInstallDirectoryLooksValid(installLocation)) {
                    candidateInstallDir = installLocation;
                }
                std::filesystem::path localManifest = PathFromUtf8(installLocation) / "install.manifest.json";
                if (std::filesystem::exists(localManifest)) {
                    candidateManifest = Utf8FromPath(localManifest);
                    if (candidateInstallDir.empty()) {
                        candidateInstallDir = installLocation;
                    }
                }
            }
        }

        if (candidateManifest.empty()) {
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
                            candidateManifest = Utf8FromPath(localManifest);
                            if (candidateInstallDir.empty()) {
                                candidateInstallDir = Utf8FromPath(baseDir);
                            }
                        }
                    }
                }
            }
        }

        if (candidateManifest.empty()) {
            std::string defaultManifest = getDefaultManifestPath(identity, resolver);
            if (!defaultManifest.empty() && std::filesystem::exists(PathFromUtf8(defaultManifest))) {
                candidateManifest = defaultManifest;
            }
        }

        if (!candidateManifest.empty()) {
            json manifest;
            if (readManifest(candidateManifest, manifest)) {
                std::string manifestInstallDir = manifest.value("installDir", "");
                if (!manifestInstallDir.empty() && ExistingInstallDirectoryLooksValid(manifestInstallDir)) {
                    candidateInstallDir = manifestInstallDir;
                }
            }
        }

        if (!candidateManifest.empty() || ExistingInstallDirectoryLooksValid(candidateInstallDir)) {
            manifestPath = std::move(candidateManifest);
            installDir = std::move(candidateInstallDir);
            if (matchedIdentity) {
                *matchedIdentity = identity;
            }
            return true;
        }
    }

    return false;
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

    std::wstring helperPath = std::wstring(tempFile) + L".exe";
    DeleteFileW(tempFile);
    if (!CopyFileW(exePathW.c_str(), helperPath.c_str(), FALSE)) {
        return false;
    }
    auto quoteArg = [](const std::wstring& value) {
        std::wstring quoted = L"\"";
        for (wchar_t ch : value) {
            if (ch == L'"') {
                quoted += L'\\';
            }
            quoted += ch;
        }
        quoted += L"\"";
        return quoted;
    };

    std::wstring cmd = quoteArg(helperPath) +
                       L" --cleanup-self --cleanup-parent-pid " +
                       std::to_wstring(GetCurrentProcessId()) +
                       L" --cleanup-exe " + quoteArg(exePathW);
    if (!manifestPath.empty()) {
        std::wstring manifestW = Utf8ToWide(manifestPath);
        if (!manifestW.empty()) {
            cmd += L" --cleanup-manifest " + quoteArg(manifestW);
        }
    }
    for (const auto& root : cleanupRoots) {
        if (root.empty()) {
            continue;
        }
        std::wstring rootW = Utf8ToWide(root);
        if (!rootW.empty()) {
            cmd += L" --cleanup-root " + quoteArg(rootW);
        }
    }

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');
    BOOL ok = CreateProcessW(helperPath.c_str(), cmdLine.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, tempPath, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        DeleteFileW(helperPath.c_str());
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

#ifdef _WIN32
static bool executeShellCommandWithTimeout(const std::string& command,
                                           const std::string& workingDirectory,
                                           bool wait,
                                           uint32_t timeoutSec,
                                           const std::function<bool()>& cancellationCallback,
                                           DWORD& exitCode,
                                           std::string& error) {
    if (command.empty()) {
        error = "Component uninstall command is empty.";
        return false;
    }

    std::wstring commandLine = L"cmd.exe /c ";
    commandLine += Utf8ToWide(command);
    std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
    commandBuffer.push_back(L'\0');

    std::wstring workDirW = Utf8ToWide(workingDirectory);
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    BOOL ok = CreateProcessW(nullptr,
                             commandBuffer.data(),
                             nullptr,
                             nullptr,
                             FALSE,
                             CREATE_NO_WINDOW,
                             nullptr,
                             workDirW.empty() ? nullptr : workDirW.c_str(),
                             &si,
                             &pi);
    if (!ok) {
        error = "Failed to start component uninstall command.";
        return false;
    }

    CloseHandle(pi.hThread);
    pi.hThread = nullptr;

    if (!wait) {
        exitCode = 0;
        CloseHandle(pi.hProcess);
        return true;
    }

    const uint64_t timeoutMs = timeoutSec == 0
                                   ? std::numeric_limits<uint64_t>::max()
                                   : static_cast<uint64_t>(timeoutSec) * 1000ULL;
    uint64_t elapsedMs = 0;
    while (true) {
        if (cancellationCallback && cancellationCallback()) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            error = "Component uninstall cancelled.";
            return false;
        }

        DWORD slice = 200;
        if (timeoutMs != std::numeric_limits<uint64_t>::max()) {
            if (elapsedMs >= timeoutMs) {
                TerminateProcess(pi.hProcess, 1);
                CloseHandle(pi.hProcess);
                error = "Component uninstall timed out.";
                return false;
            }
            uint64_t remaining = timeoutMs - elapsedMs;
            if (remaining < slice) {
                slice = static_cast<DWORD>(remaining);
            }
        }

        DWORD waitResult = WaitForSingleObject(pi.hProcess, slice);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult != WAIT_TIMEOUT) {
            CloseHandle(pi.hProcess);
            error = "Failed while waiting for component uninstall command.";
            return false;
        }
        elapsedMs += slice;
    }

    if (!GetExitCodeProcess(pi.hProcess, &exitCode)) {
        CloseHandle(pi.hProcess);
        error = "Failed to read component uninstall exit code.";
        return false;
    }
    CloseHandle(pi.hProcess);
    return true;
}
#endif

bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           ConsoleInterface& console) {
    return uninstallFromManifest(manifestPath, resolver, console, {}, {});
}

bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           ConsoleInterface& console,
                           const UninstallProgressCallback& progressCallback,
                           const std::function<bool()>& cancellationCallback) {
    auto isCancelled = [&]() {
        return cancellationCallback && cancellationCallback();
    };

    size_t totalUnits = 0;
    size_t completedUnits = 0;
    float reportedProgress = 0.0f;

    auto emitProgress = [&](const std::string& item) {
        if (!progressCallback) {
            return;
        }
        float progress = 1.0f;
        if (totalUnits > 0) {
            progress = static_cast<float>(completedUnits) / static_cast<float>(totalUnits);
        }
        if (progress < reportedProgress) {
            progress = reportedProgress;
        }
        if (progress > 1.0f) {
            progress = 1.0f;
        }
        reportedProgress = progress;

        UninstallProgressInfo info;
        info.progress = progress;
        info.currentItem = item;
        progressCallback(info);
    };

    auto addWorkUnits = [&](size_t units) {
        totalUnits += units;
    };

    auto completeWorkUnit = [&](const std::string& item) {
        if (completedUnits < totalUnits) {
            ++completedUnits;
        }
        emitProgress(item);
    };

    json manifest;
    if (!readManifest(manifestPath, manifest)) {
        console.showError("Failed to read manifest: " + manifestPath);
        return false;
    }
    console.showInfo("Loaded manifest: " + manifestPath);

    std::string appId = GetManifestAppId(manifest);
    std::string displayName = GetManifestDisplayName(manifest);
    std::vector<std::string> legacyAppIds = GetManifestLegacyAppIds(manifest);
    std::vector<std::string> identityCandidates =
        buildIdentityCandidates(appId, legacyAppIds, displayName);
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

    std::vector<ComponentExecutionRecord> componentActions;
    if (manifest.contains("componentActions") && manifest["componentActions"].is_array()) {
        for (const auto& item : manifest["componentActions"]) {
            if (!item.is_object()) {
                continue;
            }
            ComponentExecutionRecord record;
            record.componentId = item.value("componentId", "");
            record.sourceType = item.value("sourceType", "");
            record.uninstallCommand = item.value("uninstallCommand", "");
            record.workingDirectory = item.value("workingDirectory", "");
            record.wait = item.value("wait", true);
            record.timeoutSec = item.value("timeoutSec", static_cast<uint32_t>(900));
            if (!record.uninstallCommand.empty()) {
                componentActions.push_back(std::move(record));
            }
        }
    }

    std::vector<RegistryEntry> manifestRegistryEntries;
    if (manifest.contains("registry") && manifest["registry"].is_array()) {
        for (const auto& reg : manifest["registry"]) {
            RegistryEntry entry;
            entry.path = reg.value("path", "");
            entry.key = reg.value("key", "");
            manifestRegistryEntries.push_back(entry);
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
    if (!displayName.empty()) {
        std::string appLower = displayName;
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

    std::vector<std::string> killTargets = buildKillProcessList(displayName, installKillProcesses);

    addWorkUnits(2); // install state: uninstalling + uninstalled
    if (!killTargets.empty()) {
        addWorkUnits(1);
    }
    if (autoStartup && !displayName.empty()) {
        addWorkUnits(1);
    }
    if (desktopIcons && !displayName.empty()) {
        addWorkUnits(1);
    }
    addWorkUnits(componentActions.size());
    addWorkUnits(manifestRegistryEntries.size());
#ifdef _WIN32
    if (!identityCandidates.empty()) {
        addWorkUnits(1);
    }
#endif
    if (!uninstallPath.empty()) {
        addWorkUnits(1);
    }
    addWorkUnits(files.size());
    addWorkUnits(cleanupRoots.size() * 2); // scan root + remove root
    if (!manifestPath.empty()) {
        addWorkUnits(1);
    }
    if (!identityCandidates.empty()) {
        addWorkUnits(1);
    }

    emitProgress("Preparing old installation cleanup");

    if (isCancelled()) {
        console.showWarning("Uninstall cancelled before cleanup started");
        return false;
    }

    std::vector<std::string> running;
    if (!killTargets.empty()) {
        running = getRunningProcessesByName(killTargets);
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
        completeWorkUnit("Terminating running processes");
    }

    if (isCancelled()) {
        console.showWarning("Uninstall cancelled");
        return false;
    }

    applyInstallState(installState, "uninstalling", resolver);
    completeWorkUnit("Marking uninstalling state");

    for (const auto& action : componentActions) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while running component uninstall actions");
            return false;
        }

        std::string label = action.componentId.empty() ? action.sourceType : action.componentId;
        if (label.empty()) {
            label = "component";
        }

#ifdef _WIN32
        DWORD exitCode = 0;
        std::string commandError;
        bool ok = executeShellCommandWithTimeout(action.uninstallCommand,
                                                 action.workingDirectory,
                                                 action.wait,
                                                 action.timeoutSec,
                                                 cancellationCallback,
                                                 exitCode,
                                                 commandError);
        if (!ok) {
            console.showWarning("Component uninstall command failed (" + label + "): " + commandError);
        } else if (action.wait && exitCode != 0) {
            console.showWarning("Component uninstall command returned non-zero exit code (" + label +
                                "): " + std::to_string(exitCode));
        } else {
            console.showInfo("Component uninstall command completed: " + label);
        }
#else
        console.showWarning("Component uninstall actions are supported on Windows only. skipped: " + label);
#endif

        completeWorkUnit("Replaying component uninstall action: " + label);
    }

    if (autoStartup && !displayName.empty()) {
        removeAutoStartup(displayName);
        completeWorkUnit("Removing auto startup entry");
    }
    if (desktopIcons && !displayName.empty()) {
        deleteDesktopShortcut(displayName);
        completeWorkUnit("Removing desktop shortcut");
    }

    for (const auto& entry : manifestRegistryEntries) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while removing registry values");
            return false;
        }
        deleteRegistryValue(entry);
        std::string keyName = entry.key.empty() ? entry.path : (entry.path + "\\" + entry.key);
        completeWorkUnit("Removing registry value: " + keyName);
    }

#ifdef _WIN32
    if (!identityCandidates.empty()) {
        bool perMachine = isRunningAsAdmin();
        for (size_t i = 0; i < identityCandidates.size(); ++i) {
            const bool removedPrimary = deleteUninstallRegistryEntry(identityCandidates[i], perMachine);
            const bool removedSecondary = deleteUninstallRegistryEntry(identityCandidates[i], !perMachine);
            if (i == 0) {
                removedUninstall = removedPrimary || removedSecondary;
            }
        }
        deleteMatchingUninstallRegistryEntries(installDir, uninstallPath, perMachine);
        deleteMatchingUninstallRegistryEntries(installDir, uninstallPath, !perMachine);
        completeWorkUnit("Removing uninstall registry entry");
    }
#endif

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
        completeWorkUnit("Removing uninstall executable");
    }

    for (const auto& file : files) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while deleting files");
            return false;
        }
        std::string fileNorm = normalizePathForCompare(file);
        if (!currentExe.empty() && fileNorm == currentExeNorm) {
            console.showInfo("Skipping file removal (current exe): " + file);
            completeWorkUnit("Skipping current running executable");
            continue;
        }
        std::filesystem::path path = PathFromUtf8(file);
        std::error_code removeEc;
        std::filesystem::remove(toLongPath(path), removeEc);
        if (removeEc && std::filesystem::exists(path)) {
            console.showWarning("Failed to remove file: " + file);
        }
        completeWorkUnit("Removing old file: " + file);
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
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while cleaning directories");
            return false;
        }

        std::filesystem::path rootPath = PathFromUtf8(root);
        completeWorkUnit("Scanning old directory: " + root);

        if (!std::filesystem::exists(rootPath)) {
            completeWorkUnit("Directory already removed: " + root);
            continue;
        }

        std::vector<std::filesystem::path> emptyDirs;
        std::vector<std::filesystem::path> ignoredFiles;
        auto cleanupStart = std::chrono::steady_clock::now();
        console.showInfo("Cleanup dirs start: " + root);
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(toLongPath(rootPath), options)) {
            if (entry.is_regular_file()) {
                std::string name = Utf8FromPath(entry.path().filename());
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "desktop.ini" || name == "thumbs.db") {
                    ignoredFiles.push_back(entry.path());
                    addWorkUnits(1);
                }
            }
            if (entry.is_directory()) {
                emptyDirs.push_back(entry.path());
                addWorkUnits(1);
            }
            if (isCancelled()) {
                console.showWarning("Uninstall cancelled during directory scan");
                return false;
            }
        }

        for (const auto& ignored : ignoredFiles) {
            std::error_code ec;
            std::filesystem::remove(toLongPath(ignored), ec);
            completeWorkUnit("Removing ignored file: " + Utf8FromPath(ignored));
        }

        std::sort(emptyDirs.begin(), emptyDirs.end(), [](const auto& a, const auto& b) {
            return a.native().size() > b.native().size();
        });
        for (const auto& dir : emptyDirs) {
            if (isCancelled()) {
                console.showWarning("Uninstall cancelled during directory removal");
                return false;
            }
            std::error_code ec;
            std::filesystem::remove(toLongPath(dir), ec);
            if (ec && std::filesystem::exists(dir)) {
                console.showWarning("Failed to remove empty directory: " + Utf8FromPath(dir));
            }
            completeWorkUnit("Removing old directory: " + Utf8FromPath(dir));
        }

        std::error_code rootEc;
        std::filesystem::remove(toLongPath(rootPath), rootEc);
        if (rootEc && std::filesystem::exists(rootPath)) {
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

        completeWorkUnit("Removing install root: " + root);
    }

    removeInstallStateArtifacts(installState, resolver);
    completeWorkUnit("Removing install state artifacts");

    applyInstallState(installState, "uninstalled", resolver);
    completeWorkUnit("Marking uninstalled state");

    if (!manifestPath.empty()) {
        if (!std::filesystem::remove(toLongPath(PathFromUtf8(manifestPath)))) {
            if (std::filesystem::exists(PathFromUtf8(manifestPath))) {
                console.showWarning("Failed to remove manifest: " + manifestPath);
            }
        }
        completeWorkUnit("Removing uninstall manifest");
    }

    for (const auto& identity : identityCandidates) {
        std::string defaultPath = getDefaultManifestPath(identity, resolver);
        if (!defaultPath.empty() && defaultPath != manifestPath) {
            std::error_code removeEc;
            std::filesystem::remove(toLongPath(PathFromUtf8(defaultPath)), removeEc);
            if (removeEc && std::filesystem::exists(PathFromUtf8(defaultPath))) {
                console.showWarning("Failed to remove legacy default manifest: " + defaultPath);
            }
        }
    }
    completeWorkUnit("Removing default manifest pointer");

    std::filesystem::path exePath = PathFromUtf8(getCurrentExecutablePath());
    std::string exeName = Utf8FromPath(exePath.filename());
    std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
    if (exeName == "uninstall.exe") {
        addWorkUnits(1);
        if (!scheduleSelfDeleteImmediate(cleanupRoots, manifestPath)) {
            if (!scheduleSelfDelete()) {
                console.showWarning("Failed to schedule uninstall.exe removal");
            }
        } else {
            console.showInfo("Scheduled immediate uninstall.exe removal");
        }
        completeWorkUnit("Scheduling self cleanup");
    }

    if (completedUnits < totalUnits) {
        completedUnits = totalUnits;
    }
    emitProgress("Old installation cleanup completed");

    console.showInfo("Uninstall completed");
    return true;
}

} // namespace MultiThreadedInstaller
