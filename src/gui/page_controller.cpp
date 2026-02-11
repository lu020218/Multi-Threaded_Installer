#ifdef GUI_ENABLED

#include "../../include/gui/page_controller.h"
#include "../../include/gui/installation_worker.h"
#include "../../include/gui/license_dialog.h"
#include <chrono>
#include <thread>

using namespace DuiLib;

namespace MultiThreadedInstaller {

PageController::PageController(CTabLayoutUI* pTabLayout)
    : m_pTabLayout(pTabLayout)
    , m_currentPage(PageType::Welcome)
    , m_pWorker(nullptr) {

    if (m_pTabLayout) {
        m_pTabLayout->SelectItem(static_cast<int>(PageType::Welcome));
    }
}

PageController::~PageController() {

    if (m_pWorker) {
        delete m_pWorker;
        m_pWorker = nullptr;
    }
}

void PageController::NavigateToPage(PageType pageType) {
    if (!m_pTabLayout) {
        return;
    }
    

    m_currentPage = pageType;
    

    ApplyTransitionAnimation();
    

    m_pTabLayout->SelectItem(static_cast<int>(pageType));
}

PageType PageController::GetCurrentPage() const {
    return m_currentPage;
}

void PageController::ApplyTransitionAnimation() {
    if (!m_pTabLayout) {
        return;
    }
    



    


    CControlUI* pCurrentPage = m_pTabLayout->GetItemAt(m_pTabLayout->GetCurSel());
    if (pCurrentPage) {



    }
    


}

bool PageController::ShowLicenseDialog(HWND hParent) {

    LicenseDialog* pDialog = new LicenseDialog();
    

    bool agreed = pDialog->ShowModal(hParent);
    

    delete pDialog;
    
    return agreed;
}

void PageController::StartInstallation(const std::wstring& installPath,
                                       bool autoRun,
                                       bool desktopIcons,
                                       const std::wstring& languageCode,
                                       bool cleanupOldInstall,
                                       HWND hNotifyWindow) {

    if (m_pWorker) {
        delete m_pWorker;
        m_pWorker = nullptr;
    }
    

    m_pWorker = new InstallationWorker(hNotifyWindow);
    



    
    m_pWorker->StartInstallation(installPath, autoRun, desktopIcons, languageCode,
                                 cleanupOldInstall);


    NavigateToPage(PageType::Progress);
}

void PageController::OnInstallationComplete(bool success, const std::wstring& errorMsg) {

    NavigateToPage(PageType::Completion);
    


    

}

void PageController::OnProgressUpdate(const std::wstring& currentFolder, float progress) {


    

    if (progress < 0.0f) {
        progress = 0.0f;
    }
    if (progress > 100.0f) {
        progress = 100.0f;
    }
    

}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
