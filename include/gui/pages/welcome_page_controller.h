#pragma once

#include <UIlib.h>
#include <string>


namespace MultiThreadedInstaller {

/**
 *
 *
 */
class WelcomePageController {
public:
    WelcomePageController();
    ~WelcomePageController();
    

    void Initialize(DuiLib::CPaintManagerUI* pManager);
    

    void UpdateDiskSpaceInfo(const std::wstring& path);
    

    void UpdateInstallButtonState();
    

    std::wstring GetInstallPath() const;
    

    bool IsLicenseAgreed() const;
    

    void SetRequiredDiskSpace(uint64_t bytes);
    

    void SetDefaultInstallPath(const std::wstring& path);
    
private:

    DuiLib::CEditUI* m_pInstallPathEdit;
    DuiLib::CCheckBoxUI* m_pLicenseCheckbox;
    DuiLib::CButtonUI* m_pInstallButton;
    DuiLib::CLabelUI* m_pDiskSpaceLabel;
    

    uint64_t m_requiredDiskSpace;
    uint64_t m_availableDiskSpace;
    bool m_hasEnoughSpace;
    

    uint64_t GetAvailableDiskSpace(const std::wstring& path);
    std::wstring FormatBytes(uint64_t bytes);
    bool ValidatePath(const std::wstring& path);
};

} // namespace MultiThreadedInstaller

