#include "../../include/gui/gui_startup_initializer.h"

#include "../../include/gui/gui_install_actions.h"
#include "../../include/gui/gui_manager.h"
#include "../../include/gui/gui_runtime_utils.h"
#include "../../include/installer/installer_helpers.h"
#include "../../include/installer/path_resolver.h"
#include "../../include/installer/registry_utils.h"
#include "../../include/installer/uninstall_manager.h"
#include "common/utf8_utils.h"

#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace DuiLib;

namespace MultiThreadedInstaller {

namespace {

std::wstring ExpandEnvVars(const std::wstring& value) {
#ifdef _WIN32
    if (value.find(L'%') == std::wstring::npos) {
        return value;
    }
    wchar_t buffer[MAX_PATH];
    DWORD len = ExpandEnvironmentStringsW(value.c_str(), buffer, MAX_PATH);
    if (len == 0 || len > MAX_PATH) {
        return value;
    }
    return std::wstring(buffer);
#else
    return value;
#endif
}

std::wstring NormalizeInstallPath(const std::wstring& basePath,
                                  const std::wstring& appName) {
    if (basePath.empty() || appName.empty()) {
        return basePath;
    }

    std::filesystem::path selectedFs = basePath;
    std::wstring appNameLower = ToLowerString(appName);
    std::wstring selectedNameLower = ToLowerString(selectedFs.filename().wstring());

    if (selectedNameLower == appNameLower) {
        return selectedFs.wstring();
    }

    std::filesystem::path childPath = selectedFs / appName;
    return childPath.wstring();
}

std::wstring ResolveEffectiveDirectoryNameFromConfig(const InstallConfig& config) {
    const std::string directoryName = resolveEffectiveDirectoryName(
        WideToUtf8(config.directoryName),
        WideToUtf8(config.applicationName));
    return Utf8ToWide(directoryName);
}

std::wstring ApplyInstallDirectoryRule(const std::wstring& basePath, const InstallConfig& config) {
    if (!config.installDirectoryAppendName) {
        return basePath;
    }
    return NormalizeInstallPath(basePath, ResolveEffectiveDirectoryNameFromConfig(config));
}

} // namespace

std::wstring ResolveInitialInstallPath(const InstallConfig& config) {
    std::wstring installPath = ExpandEnvVars(config.defaultInstallPath);

#ifdef _WIN32
    wchar_t envPath[MAX_PATH];
    DWORD envLen = GetEnvironmentVariableW(L"MTINSTALLER_INSTALL_PATH", envPath, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH) {
        installPath = envPath;
        SetEnvironmentVariableW(L"MTINSTALLER_INSTALL_PATH", nullptr);
    }
#endif

    if (!config.registryPath.empty() && !config.registryKey.empty()) {
        std::string regPath = WideToUtf8(config.registryPath);
        std::string regKey = WideToUtf8(config.registryKey);
        std::string regValue;
        if (readRegistryStringValue(regPath, regKey, regValue)) {
            std::wstring regPathW = Utf8ToWide(regValue);
            if (!regPathW.empty()) {
                installPath = regPathW;
            }
        }
    }

    InstallerPathResolver identityResolver;
    std::string previousManifest;
    std::string previousInstallDir;
    std::vector<std::string> identityCandidates =
        GUIInstallActions::BuildIdentityCandidatesFromConfig(config);
    if (resolveExistingInstallInfo(identityCandidates,
                                   identityResolver,
                                   previousManifest,
                                   previousInstallDir) &&
        !previousInstallDir.empty()) {
        return Utf8ToWide(previousInstallDir);
    }

    return ApplyInstallDirectoryRule(installPath, config);
}

void ApplyInitialInstallPathUi(CPaintManagerUI& paintManager,
                               CEditUI* installPathEdit,
                               const std::wstring& installPath,
                               bool repairMode) {
    if (installPathEdit) {
        installPathEdit->SetText(WStringToTStr(installPath));
        if (repairMode) {
            installPathEdit->SetReadOnly(true);
            installPathEdit->SetEnabled(false);
        }
    }

    if (!repairMode) {
        return;
    }

    if (CControlUI* browseButton = paintManager.FindControl(_T("browse_button"))) {
        browseButton->SetEnabled(false);
        browseButton->SetVisible(false);
    }
    if (CControlUI* browseButton = paintManager.FindControl(_T("btnSelectDir"))) {
        browseButton->SetEnabled(false);
        browseButton->SetVisible(false);
    }
}

} // namespace MultiThreadedInstaller
