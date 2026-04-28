#include "installer/install_state_utils.h"

#include "installer/registry_utils.h"
#include "common/utf8_utils.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

namespace {

std::string replaceAll(std::string value, const std::string& token, const std::string& replacement) {
    if (token.empty()) {
        return value;
    }
    size_t pos = 0;
    while ((pos = value.find(token, pos)) != std::string::npos) {
        value.replace(pos, token.size(), replacement);
        pos += replacement.size();
    }
    return value;
}

std::string expandInstallInfoValue(const std::string& rawValue,
                                   const std::string& installDir,
                                   const std::string& version,
                                   const std::string& appName,
                                   const std::string& stateValue,
                                   InstallerPathResolver& resolver) {
    std::string expanded = rawValue;
    expanded = replaceAll(expanded, "%InstallDir%", installDir);
    expanded = replaceAll(expanded, "%Version%", version);
    expanded = replaceAll(expanded, "%AppName%", appName);
    expanded = replaceAll(expanded, "%InstallState%", stateValue);
    return resolver.expandEnvironmentVariables(expanded);
}

} // namespace

bool applyCoreInstallInfo(const InstallInfoConfig& config,
                          const std::string& installDir,
                          const std::string& version,
                          const std::string& appName,
                          const std::string& stateValue,
                          InstallerPathResolver& resolver) {
    if (config.path.empty()) {
        return false;
    }

    bool wroteAny = false;
    bool ok = true;
    for (const auto& pair : config.values) {
        const auto& valueConfig = pair.second;
        if (valueConfig.key.empty()) {
            continue;
        }
        RegistryEntry entry;
        entry.path = config.path;
        entry.key = valueConfig.key;
        entry.type = valueConfig.type;
        entry.value = valueConfig.value;

        const std::string expandedValue =
            expandInstallInfoValue(valueConfig.value, installDir, version, appName, stateValue, resolver);
        wroteAny = true;
        ok = writeRegistryValue(entry, expandedValue, entry.type) && ok;
    }

    return wroteAny && ok;
}

HANDLE acquireInstallMutex(bool useMutex, const std::string& mutexName) {
#ifdef _WIN32
    if (!useMutex || mutexName.empty()) {
        return nullptr;
    }
    std::wstring name = Utf8ToWide(mutexName);
    if (name.empty()) {
        return nullptr;
    }
    return CreateMutexW(nullptr, FALSE, name.c_str());
#else
    (void)useMutex;
    (void)mutexName;
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

bool removeInstallInfoArtifacts(const InstallInfoConfig& config) {
    if (config.path.empty()) {
        return false;
    }

    bool ok = true;
    for (const auto& pair : config.values) {
        const auto& valueConfig = pair.second;
        if (valueConfig.key.empty()) {
            continue;
        }
        RegistryEntry entry;
        entry.path = config.path;
        entry.key = valueConfig.key;
        ok = deleteRegistryValue(entry) && ok;
    }
    return ok;
}

} // namespace MultiThreadedInstaller
