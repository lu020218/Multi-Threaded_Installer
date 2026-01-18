#ifdef GUI_ENABLED

#include "../../include/gui/welcome_page_controller.h"
#include <Windows.h>
#include <sstream>
#include <iomanip>

namespace MultiThreadedInstaller {

WelcomePageController::WelcomePageController()
    : m_pInstallPathEdit(nullptr)
    , m_pLicenseCheckbox(nullptr)
    , m_pInstallButton(nullptr)
    , m_pDiskSpaceLabel(nullptr)
    , m_requiredDiskSpace(100 * 1024 * 1024) // 默认100MB
    , m_availableDiskSpace(0)
    , m_hasEnoughSpace(false) {
}

WelcomePageController::~WelcomePageController() {
    // 控件由DuiLib管理，不需要手动释放
}

void WelcomePageController::Initialize(CPaintManagerUI* pManager) {
    if (!pManager) {
        return;
    }
    
    // 获取控件指针
    m_pInstallPathEdit = static_cast<CEditUI*>(pManager->FindControl(_T("install_path")));
    m_pLicenseCheckbox = static_cast<CCheckBoxUI*>(pManager->FindControl(_T("license_checkbox")));
    m_pInstallButton = static_cast<CButtonUI*>(pManager->FindControl(_T("install_button")));
    m_pDiskSpaceLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("disk_space_info")));
    
    // 初始化时更新磁盘空间信息
    if (m_pInstallPathEdit) {
        CDuiString pathStr = m_pInstallPathEdit->GetText();
        std::wstring path(pathStr.GetData());
        UpdateDiskSpaceInfo(path);
    }
    
    // 初始化按钮状态
    UpdateInstallButtonState();
}

void WelcomePageController::UpdateDiskSpaceInfo(const std::wstring& path) {
    if (!m_pDiskSpaceLabel || path.empty()) {
        return;
    }
    
    // 验证路径
    if (!ValidatePath(path)) {
        m_pDiskSpaceLabel->SetText(_T("无效的安装路径"));
        m_hasEnoughSpace = false;
        UpdateInstallButtonState();
        return;
    }
    
    // 获取可用磁盘空间
    m_availableDiskSpace = GetAvailableDiskSpace(path);
    
    // 检查空间是否充足
    m_hasEnoughSpace = (m_availableDiskSpace >= m_requiredDiskSpace);
    
    // 格式化显示文本
    std::wstring requiredStr = FormatBytes(m_requiredDiskSpace);
    std::wstring availableStr = FormatBytes(m_availableDiskSpace);
    
    std::wstringstream ss;
    ss << L"所需空间: " << requiredStr << L" | 可用空间: " << availableStr;
    
    if (!m_hasEnoughSpace) {
        ss << L" (空间不足!)";
    }
    
    CDuiString displayText(ss.str().c_str());
    m_pDiskSpaceLabel->SetText(displayText);
    
    // 更新按钮状态
    UpdateInstallButtonState();
}

void WelcomePageController::UpdateInstallButtonState() {
    if (!m_pInstallButton) {
        return;
    }
    
    // 安装按钮启用条件：
    // 1. 许可协议已同意
    // 2. 磁盘空间充足
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
    
    // 重新验证磁盘空间
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
    
    // 提取驱动器根路径
    std::wstring rootPath;
    if (path.length() >= 2 && path[1] == L':') {
        rootPath = path.substr(0, 2) + L"\\";
    } else if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        // UNC路径
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
    
    // 使用GetDiskFreeSpaceEx查询可用空间
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
    
    // 检查路径格式
    // 1. 绝对路径（C:\...）
    if (path.length() >= 3 && path[1] == L':' && path[2] == L'\\') {
        // 检查驱动器字母是否有效
        wchar_t drive = path[0];
        if ((drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z')) {
            return true;
        }
    }
    
    // 2. UNC路径（\\server\share\...）
    if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        return true;
    }
    
    return false;
}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
