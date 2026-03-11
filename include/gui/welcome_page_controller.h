#pragma once

#include <UIlib.h>
#include <string>

using namespace DuiLib;

namespace MultiThreadedInstaller {

/**
 *
 *
 */
class WelcomePageController {
public:
    WelcomePageController();
    ~WelcomePageController();
    

    void Initialize(CPaintManagerUI* pManager);
    

    void UpdateDiskSpaceInfo(const std::wstring& path);
    

    void UpdateInstallButtonState();
    

    std::wstring GetInstallPath() const;
    

    bool IsLicenseAgreed() const;
    

    void SetRequiredDiskSpace(uint64_t bytes);
    

    void SetDefaultInstallPath(const std::wstring& path);
    
private:

    CEditUI* m_pInstallPathEdit;
    CCheckBoxUI* m_pLicenseCheckbox;
    CButtonUI* m_pInstallButton;
    CLabelUI* m_pDiskSpaceLabel;
    

    uint64_t m_requiredDiskSpace;
    uint64_t m_availableDiskSpace;
    bool m_hasEnoughSpace;
    

    uint64_t GetAvailableDiskSpace(const std::wstring& path);
    std::wstring FormatBytes(uint64_t bytes);
    bool ValidatePath(const std::wstring& path);
};

} // namespace MultiThreadedInstaller

