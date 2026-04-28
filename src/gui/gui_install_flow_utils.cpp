#include "gui/gui_install_flow_utils.h"

#include "gui/gui_helpers.h"
#include "gui/gui_runtime_utils.h"
#include "installer/path_resolver.h"
#include "installer/installer_helpers.h"
#include "installer/uninstall_manager.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"

#include <filesystem>
#include <sstream>

namespace MultiThreadedInstaller::GUIInstallFlowUtils {

namespace {

std::wstring ResolveEffectiveDirectoryNameFromConfig(const InstallConfig& config) {
    const std::string directoryName = resolveEffectiveDirectoryName(
        WideToUtf8(config.directoryName), WideToUtf8(config.applicationName));
    return Utf8ToWide(directoryName);
}

} // namespace

bool ConfirmCleanupOldInstall(HWND hWnd,
                              const ExtendedInstallationMetadata& metadata,
                              const std::wstring& installPath) {
    if (metadata.installAutoCleanOldInstall) {
        return true;
    }

    InstallerPathResolver pathResolver;
    const std::string installPathUtf8 = WideToUtf8(installPath);
    const std::string resolvedInstallRoot = pathResolver.expandEnvironmentVariables(installPathUtf8);

    std::string previousManifest;
    std::string previousInstallDir;
    const auto installDirIt = metadata.installInfo.values.find("installDir");
    if (installDirIt == metadata.installInfo.values.end() ||
        !resolveInstallInfoFromRegistry(metadata.installInfo.path,
                                        installDirIt->second.key,
                                        previousManifest,
                                        previousInstallDir)) {
        return false;
    }

    const std::string newPath = resolvedInstallRoot.empty() ? installPathUtf8 : resolvedInstallRoot;
    const std::string normalizedOld = normalizePathForCompare(previousInstallDir);
    const std::string normalizedNew = normalizePathForCompare(newPath);
    if (normalizedOld.empty() || normalizedNew.empty() || normalizedOld == normalizedNew) {
        return false;
    }

    if (previousManifest.empty()) {
        logInstallerInfo("[GUI] Old install manifest not found; skipping cleanup prompt.");
        return false;
    }

    std::wstring title = GUIHelpers::GetLocalizedText(L"msg.dialog.cleanup_old.title", L"");
    std::wstring yesText = GUIHelpers::GetLocalizedText(L"msg.dialog.cleanup_old.yes", L"");
    std::wstring noText = GUIHelpers::GetLocalizedText(L"msg.dialog.cleanup_old.no", L"");
    std::wstring message = GUIHelpers::GetLocalizedText(L"msg.dialog.cleanup_old.message", L"");
    DialogResult result =
        GUIHelpers::ShowCustomDialog(hWnd, title, message, yesText, noText, L"");
    return result == DialogResult::Ok;
}

std::wstring ResolveSelectedInstallPath(const InstallConfig& config,
                                        const std::wstring& selectedPath) {
    (void)config;
    return selectedPath;
}

void UpdateInstallButtonEnabled(CButtonUI* installButton,
                                CCheckBoxUI* licenseCheckbox,
                                CEditUI* installPathEdit,
                                uint64_t requiredDiskSpace) {
    if (!installButton || !licenseCheckbox || !installPathEdit) {
        return;
    }

    const bool licenseAgreed = licenseCheckbox->GetCheck();
    const std::wstring installPath = installPathEdit->GetText().GetData();
    uint64_t availableSpace = 0;
    const bool spaceEnough =
        GUIHelpers::CheckDiskSpace(installPath, requiredDiskSpace, availableSpace);
    installButton->SetEnabled(licenseAgreed && spaceEnough);
}

void UpdateDiskSpaceLabel(CLabelUI* diskSpaceLabel,
                          const std::wstring& path,
                          uint64_t requiredDiskSpace) {
    if (!diskSpaceLabel) {
        return;
    }

    const uint64_t totalSpace = GUIHelpers::GetTotalDiskSpace(path);
    const uint64_t availableSpace = GUIHelpers::GetAvailableDiskSpace(path);
    const std::wstring totalStr = GUIHelpers::FormatBytes(totalSpace);
    const std::wstring freeStr = GUIHelpers::FormatBytes(availableSpace);
    const std::wstring requiredStr = GUIHelpers::FormatBytes(requiredDiskSpace);

    const std::wstring totalLabel = GUIHelpers::GetLocalizedText(L"msg.space.total", L"");
    const std::wstring freeLabel = GUIHelpers::GetLocalizedText(L"msg.space.free", L"");
    const std::wstring requiredLabel = GUIHelpers::GetLocalizedText(L"msg.space.required", L"");

    std::wstringstream ss;
    if (totalSpace > 0) {
        ss << totalLabel << totalStr << L" | " << freeLabel << freeStr;
    } else {
        ss << freeLabel << freeStr;
    }
    ss << L" | " << requiredLabel << requiredStr;

    diskSpaceLabel->SetText(ss.str().c_str());
    diskSpaceLabel->SetTextColor(availableSpace < requiredDiskSpace ? 0xFFFF0000 : 0xFF666666);
}

} // namespace MultiThreadedInstaller::GUIInstallFlowUtils
