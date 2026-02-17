#pragma once

#ifdef GUI_ENABLED

#include <UIlib.h>
#include <string>
#include <vector>

using namespace DuiLib;

namespace MultiThreadedInstaller {


enum class PageType {
    Welcome = 0,
    License = 1,
    Progress = 2,
    Completion = 3
};

// Forward declarations
class InstallationWorker;

class PageController {
public:
    PageController(CTabLayoutUI* pTabLayout);
    ~PageController();
    

    void NavigateToPage(PageType pageType);
    

    PageType GetCurrentPage() const;
    

    bool ShowLicenseDialog(HWND hParent);
    

    void StartInstallation(const std::wstring& installPath,
                           bool autoRun,
                           bool desktopIcons,
                           const std::wstring& languageCode,
                           bool cleanupOldInstall,
                           const std::vector<std::string>& selectedComponents,
                           HWND hNotifyWindow);
    

    void OnInstallationComplete(bool success, const std::wstring& errorMsg);
    

    void OnProgressUpdate(const std::wstring& currentFolder, float progress);
    
private:
    CTabLayoutUI* m_pTabLayout;
    PageType m_currentPage;
    InstallationWorker* m_pWorker;
    

    void ApplyTransitionAnimation();
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
