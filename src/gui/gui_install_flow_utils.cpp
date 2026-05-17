#include "gui/gui_install_flow_utils.h"

#include "gui/gui_helpers.h"
#include "gui/gui_runtime_utils.h"
#include "common/utf8_utils.h"
#include "installer/installer_helpers.h"

#include <sstream>

namespace MultiThreadedInstaller::GUIInstallFlowUtils {

namespace {

std::wstring ResolveDirectoryNameFromConfig(const InstallConfig& config) {
    return config.directoryName;
}

} // namespace
std::wstring ResolveSelectedInstallPath(const InstallConfig& config,
                                        const std::wstring& selectedPath) {
    const std::wstring directoryName = ResolveDirectoryNameFromConfig(config);
    if (selectedPath.empty() || directoryName.empty()) {
        return selectedPath;
    }

    const std::string adjustedPath =
        appendPathLeafIfMissing(WideToUtf8(selectedPath), WideToUtf8(directoryName));
    return Utf8ToWide(adjustedPath);
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
