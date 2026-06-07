#include "gui/pages/welcome_page_controller.h"
#include "gui/core/gui_helpers.h"
#include <Windows.h>
#include <sstream>
#include <iomanip>

using namespace DuiLib;

namespace MultiThreadedInstaller {

WelcomePageController::WelcomePageController()
    : m_pInstallPathEdit(nullptr)
    , m_pLicenseCheckbox(nullptr)
    , m_pInstallButton(nullptr)
    , m_pDiskSpaceLabel(nullptr)
    , m_requiredDiskSpace(100 * 1024 * 1024)
    , m_availableDiskSpace(0)
    , m_hasEnoughSpace(false) {
}

WelcomePageController::~WelcomePageController() {

}

void WelcomePageController::Initialize(CPaintManagerUI* pManager) {
    if (!pManager) {
        return;
    }
    

    m_pInstallPathEdit = static_cast<CEditUI*>(pManager->FindControl(_T("install_path")));
    m_pLicenseCheckbox = static_cast<CCheckBoxUI*>(pManager->FindControl(_T("license_checkbox")));
    m_pInstallButton = static_cast<CButtonUI*>(pManager->FindControl(_T("install_button")));
    m_pDiskSpaceLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("disk_space_info")));
    

    if (m_pInstallPathEdit) {
        CDuiString pathStr = m_pInstallPathEdit->GetText();
        std::wstring path(pathStr.GetData());
        UpdateDiskSpaceInfo(path);
    }
    

    UpdateInstallButtonState();
}

void WelcomePageController::UpdateDiskSpaceInfo(const std::wstring& path) {
    if (!m_pDiskSpaceLabel || path.empty()) {
        return;
    }
    

    if (!ValidatePath(path)) {
        std::wstring invalidPathText = GUIHelpers::GetLocalizedText(
            L"msg.welcome.invalid_install_path", L"");
        m_pDiskSpaceLabel->SetText(invalidPathText.c_str());
        m_hasEnoughSpace = false;
        UpdateInstallButtonState();
        return;
    }
    

    m_availableDiskSpace = GetAvailableDiskSpace(path);
    

    m_hasEnoughSpace = (m_availableDiskSpace >= m_requiredDiskSpace);
    

    std::wstring requiredStr = FormatBytes(m_requiredDiskSpace);
    std::wstring availableStr = FormatBytes(m_availableDiskSpace);
    
    std::wstring requiredLabel = GUIHelpers::GetLocalizedText(
        L"msg.space.required", L"");
    std::wstring availableLabel = GUIHelpers::GetLocalizedText(
        L"msg.space.available", L"");
    std::wstring insufficientSuffix = GUIHelpers::GetLocalizedText(
        L"msg.welcome.space_insufficient", L"");

    std::wstringstream ss;
    ss << requiredLabel << requiredStr << L" | " << availableLabel << availableStr;

    if (!m_hasEnoughSpace) {
        ss << insufficientSuffix;
    }
    
    CDuiString displayText(ss.str().c_str());
    m_pDiskSpaceLabel->SetText(displayText);
    

    UpdateInstallButtonState();
}

void WelcomePageController::UpdateInstallButtonState() {
    if (!m_pInstallButton) {
        return;
    }
    



    bool licenseAgreed = IsLicenseAgreed();
    bool shouldEnable = licenseAgreed && m_hasEnoughSpace;
    
    m_pInstallButton->SetEnabled(shouldEnable);
}

std::wstring WelcomePageController::GetInstallPath() const {
    if (!m_pInstallPathEdit) {
        return L"";
    }
    
    CDuiString pathStr = m_pInstallPathEdit->GetText();
    return std::wstring(pathStr.GetData());
}

bool WelcomePageController::IsLicenseAgreed() const {
    if (!m_pLicenseCheckbox) {
        return false;
    }
    
    return m_pLicenseCheckbox->GetCheck();
}

void WelcomePageController::SetRequiredDiskSpace(uint64_t bytes) {
    m_requiredDiskSpace = bytes;
    

    if (m_pInstallPathEdit) {
        UpdateDiskSpaceInfo(m_pInstallPathEdit->GetText().GetData());
    }
}

void WelcomePageController::SetDefaultInstallPath(const std::wstring& path) {
    if (!m_pInstallPathEdit) {
        return;
    }
    
    CDuiString pathStr(path.c_str());
    m_pInstallPathEdit->SetText(pathStr);
    UpdateDiskSpaceInfo(path);
}

uint64_t WelcomePageController::GetAvailableDiskSpace(const std::wstring& path) {
    if (path.empty()) {
        return 0;
    }
    

    std::wstring rootPath;
    if (path.length() >= 2 && path[1] == L':') {
        rootPath = path.substr(0, 2) + L"\\";
    } else if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\') {

        size_t pos = path.find(L'\\', 2);
        if (pos != std::wstring::npos) {
            pos = path.find(L'\\', pos + 1);
            if (pos != std::wstring::npos) {
                rootPath = path.substr(0, pos + 1);
            }
        }
    }
    
    if (rootPath.empty()) {
        return 0;
    }
    

    ULARGE_INTEGER freeBytesAvailable;
    ULARGE_INTEGER totalNumberOfBytes;
    ULARGE_INTEGER totalNumberOfFreeBytes;
    
    if (GetDiskFreeSpaceExW(
        rootPath.c_str(),
        &freeBytesAvailable,
        &totalNumberOfBytes,
        &totalNumberOfFreeBytes)) {
        return freeBytesAvailable.QuadPart;
    }
    
    return 0;
}

std::wstring WelcomePageController::FormatBytes(uint64_t bytes) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(2) << size << L" " << units[unitIndex];
    return ss.str();
}

bool WelcomePageController::ValidatePath(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    


    if (path.length() >= 3 && path[1] == L':' && path[2] == L'\\') {

        wchar_t drive = path[0];
        if ((drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z')) {
            return true;
        }
    }
    

    if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        return true;
    }
    
    return false;
}

} // namespace MultiThreadedInstaller

