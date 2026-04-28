#include "installer/uninstall_manager.h"

#include "installer/file_system_operator.h"
#include "installer/installer_helpers.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"

#include <filesystem>
#include <fstream>

namespace MultiThreadedInstaller {

using json = nlohmann::json;

#ifdef _WIN32
namespace {

bool IsValidUtf8(const std::string& text) {
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

std::string EnsureUtf8(const std::string& text) {
    if (text.empty()) {
        return text;
    }
    if (IsValidUtf8(text)) {
        return text;
    }
    std::string utf8 = AcpToUtf8(text);
    return utf8.empty() ? text : utf8;
}

}  // namespace
#else
namespace {
std::string EnsureUtf8(const std::string& text) { return text; }
}  // namespace
#endif

namespace {

std::vector<std::string> EnsureUtf8List(const std::vector<std::string>& values) {
    std::vector<std::string> safeValues;
    safeValues.reserve(values.size());
    for (const auto& value : values) {
        safeValues.push_back(EnsureUtf8(value));
    }
    return safeValues;
}

}  // namespace

bool writeManifest(const std::string& manifestPath,
                   const std::string& appId,
                   const std::string& displayName,
                   const std::string& appVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& cleanupRoots,
                   const UninstallCleanupConfig& lifecycleUninstallCleanup,
                   const std::vector<std::string>& filePaths,
                   const std::vector<RegistryEntry>& lifecycleInstallRegistry,
                   const std::vector<std::string>& installKillProcesses,
                   bool installAutoStartup,
                   bool installDesktopIcon,
                   const std::string& desktopShortcutDisplayName,
                   const InstallInfoConfig& installInfo,
                   const std::string& uninstallPath,
                   const std::string& languageCode,
                   const std::vector<ComponentExecutionRecord>& componentActions) {
    if (manifestPath.empty()) {
        return false;
    }

    try {
        json root;
        root["version"] = "1.0";
        root["appId"] = EnsureUtf8(appId);
        root["displayName"] = EnsureUtf8(displayName);
        root["appName"] = EnsureUtf8(displayName);
        root["appVersion"] = EnsureUtf8(appVersion);
        root["installDir"] = EnsureUtf8(installDir);
        root["uninstallPath"] = EnsureUtf8(uninstallPath);
        root["cleanupRoots"] = EnsureUtf8List(cleanupRoots);
        json cleanup;
        cleanup["processes"] = json::array();
        for (const auto& process : lifecycleUninstallCleanup.processes) {
            cleanup["processes"].push_back({{"name", EnsureUtf8(process.name)}});
        }
        cleanup["registry"] = json::object();
        cleanup["registry"]["legacyKeys"] = json::array();
        for (const auto& entry : lifecycleUninstallCleanup.registry.legacyKeys) {
            cleanup["registry"]["legacyKeys"].push_back({
                {"path", EnsureUtf8(entry.path)},
                {"key", EnsureUtf8(entry.key)},
                {"value", EnsureUtf8(entry.value)},
                {"type", static_cast<int>(entry.type)}
            });
        }
        cleanup["uninstallEntries"] = json::object();
        cleanup["uninstallEntries"]["entries"] = json::array();
        for (const auto& entry : lifecycleUninstallCleanup.uninstallEntries) {
            cleanup["uninstallEntries"]["entries"].push_back({
                {"name", EnsureUtf8(entry.name)},
                {"scope", static_cast<int>(entry.scope)}
            });
        }
        cleanup["shortcuts"] = json::array();
        for (const auto& shortcut : lifecycleUninstallCleanup.shortcuts) {
            cleanup["shortcuts"].push_back({{"name", EnsureUtf8(shortcut.name)}});
        }
        cleanup["startup"] = json::array();
        for (const auto& startup : lifecycleUninstallCleanup.startup) {
            cleanup["startup"].push_back({{"name", EnsureUtf8(startup.name)}});
        }
        cleanup["paths"] = json::array();
        for (const auto& rule : lifecycleUninstallCleanup.paths) {
            cleanup["paths"].push_back({
                {"path", EnsureUtf8(rule.path)},
                {"recursive", rule.recursive},
                {"onlyIfEmpty", rule.onlyIfEmpty}
            });
        }
        root["lifecycleUninstallCleanup"] = std::move(cleanup);

        std::vector<std::string> safeFiles;
        safeFiles.reserve(filePaths.size());
        for (const auto& path : filePaths) {
            safeFiles.push_back(EnsureUtf8(path));
        }
        root["files"] = safeFiles;
        root["installAutoStartup"] = installAutoStartup;
        root["installDesktopIcon"] = installDesktopIcon;
        root["desktopShortcutDisplayName"] = EnsureUtf8(desktopShortcutDisplayName);
        root["language"] = EnsureUtf8(languageCode);

        json reg = json::array();
        for (const auto& entry : lifecycleInstallRegistry) {
            json item;
            item["path"] = EnsureUtf8(entry.path);
            item["key"] = EnsureUtf8(entry.key);
            item["value"] = EnsureUtf8(entry.value);
            item["type"] = static_cast<int>(entry.type);
            reg.push_back(item);
        }
        root["lifecycleInstallRegistry"] = reg;

        std::vector<std::string> safeInstallKill;
        safeInstallKill.reserve(installKillProcesses.size());
        for (const auto& name : installKillProcesses) {
            safeInstallKill.push_back(EnsureUtf8(name));
        }
        root["killProcesses"] = safeInstallKill;

        json actions = json::array();
        for (const auto& action : componentActions) {
            if (action.uninstallCommand.empty()) {
                continue;
            }
            json item;
            item["componentId"] = EnsureUtf8(action.componentId);
            item["sourceType"] = EnsureUtf8(action.sourceType);
            item["uninstallCommand"] = EnsureUtf8(action.uninstallCommand);
            item["workingDirectory"] = EnsureUtf8(action.workingDirectory);
            item["wait"] = action.wait;
            item["timeoutSec"] = action.timeoutSec;
            actions.push_back(item);
        }
        root["componentActions"] = actions;

        json state;
        state["mode"] = static_cast<int>(installInfo.mode);
        state["path"] = EnsureUtf8(installInfo.path);
        state["values"] = json::object();
        for (const auto& pair : installInfo.values) {
            state["values"][EnsureUtf8(pair.first)] = {
                {"key", EnsureUtf8(pair.second.key)},
                {"value", EnsureUtf8(pair.second.value)},
                {"type", static_cast<int>(pair.second.type)}
            };
        }
        root["installInfo"] = state;

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
        logInstallerError(std::string("[UNINSTALL] Failed to write manifest: ") + e.what());
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

}  // namespace MultiThreadedInstaller
