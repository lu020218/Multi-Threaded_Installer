#include "gui/core/gui_startup_initializer.h"

#include "gui/install/gui_install_actions.h"
#include "gui/core/gui_manager.h"
#include "gui/core/gui_runtime_utils.h"
#include "installer/platform/path_resolver.h"
#include "installer/state/registry_utils.h"
#include "common/installer_logger.h"
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

} // namespace

std::wstring ResolveInitialInstallPath(const InstallConfig& config) {
    return ExpandEnvVars(config.defaultInstallPath);
}

void ApplyInitialInstallPathUi(CPaintManagerUI& paintManager,
                               CEditUI* installPathEdit,
                               const std::wstring& installPath,
                               bool lockInstallPath) {
    logInstallerInfo(std::string("[GUI][Startup] ApplyInitialInstallPathUi begin lockInstallPath=") +
                     (lockInstallPath ? "true" : "false") +
                     " hasEdit=" + (installPathEdit ? "true" : "false"));
    if (installPathEdit) {
        logInstallerInfo("[GUI][Startup] Setting install path text.");
        installPathEdit->SetText(WStringToTStr(installPath));
    }
    if (lockInstallPath) {
        logInstallerInfo("[GUI][Startup] Overwrite mode uses previous install path as the default editable value.");
    }
    (void)paintManager;
    logInstallerInfo("[GUI][Startup] ApplyInitialInstallPathUi end.");
}

} // namespace MultiThreadedInstaller
