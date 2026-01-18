#ifdef GUI_ENABLED

#include "../../include/gui/gui_manager.h"
#include "../../include/gui/page_controller.h"
#include "../../include/gui/gui_helpers.h"
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <vector>

using namespace DuiLib;

namespace MultiThreadedInstaller {

// Helper function to convert wstring to string for MBCS
// Returns a static buffer that's valid until next call
static LPCTSTR WStringToTStr(const std::wstring& wstr) {
    // Use thread-local storage for the converted string
    static thread_local std::vector<std::string> stringPool;
    static thread_local size_t poolIndex = 0;
    
    // Keep a pool of 10 strings to handle multiple conversions in one statement
    if (stringPool.size() < 10) {
        stringPool.resize(10);
    }
    
    // Get next string from pool (circular)
    std::string& result = stringPool[poolIndex];
    poolIndex = (poolIndex + 1) % stringPool.size();
    
    // Convert
    if (wstr.empty()) {
        result.clear();
    } else {
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (size > 0) {
            result.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
        } else {
            result.clear();
        }
    }
    
    return (LPCTSTR)result.c_str();
}

// ���캯��
GUIManager::GUIManager()
    : m_pTabPages(nullptr),
      m_pInstallPathEdit(nullptr),
      m_pLicenseCheckbox(nullptr),
      m_pInstallButton(nullptr),
      m_pDiskSpaceLabel(nullptr),
      m_pPageController(nullptr),
      m_pWorker(nullptr) {
}

// ��������
GUIManager::~GUIManager() {
    // ����ҳ�������
    if (m_pPageController) {
        delete m_pPageController;
        m_pPageController = nullptr;
    }
    
    // ��������߳�
    if (m_pWorker) {
        delete m_pWorker;
        m_pWorker = nullptr;
    }
}

// ���ð�װ����
void GUIManager::SetInstallConfig(const InstallConfig& config) {
    m_config = config;
}

// ��ȡƤ���ļ���·��
CDuiString GUIManager::GetSkinFolder() {
    return _T("skins\\");
}

// ��ȡƤ���ļ���
CDuiString GUIManager::GetSkinFile() {
    return _T("main.xml");
}

// ��ȡ��������
LPCTSTR GUIManager::GetWindowClassName() const {
    return _T("InstallerMainWindow");
}

// ��ʼ������
void GUIManager::InitWindow() {
    // ��ʼ���ؼ�ָ��
    InitControls();
    
    // ����PageControllerʵ��
    if (m_pTabPages) {
        m_pPageController = new PageController(m_pTabPages);
    }
    
    // ����Ĭ�ϰ�װ·��
    if (m_pInstallPathEdit) {
        m_pInstallPathEdit->SetText(WStringToTStr(m_config.defaultInstallPath));
    }
    
    // ���´��̿ռ���Ϣ
    UpdateDiskSpaceInfo(m_config.defaultInstallPath);
    
    // ���°�װ��ť״̬
    UpdateInstallButtonState();
    
    // ���ý������ - ����Tab������
    // DuiLib���Զ�����tabstop="true"�Ŀؼ�
    // ���ó�ʼ���㵽��һ���ɽ����ؼ�����װ·�������
    if (m_pInstallPathEdit) {
        m_pInstallPathEdit->SetFocus();
    }
    
    // ���ھ�����ʾ
    CenterWindow();
}

// ��ʼ���ؼ�ָ��
void GUIManager::InitControls() {
    // ��ȡTabLayoutҳ������
    m_pTabPages = static_cast<CTabLayoutUI*>(
        m_pm.FindControl(_T("pages")));
    
    // ��ȡ��ӭҳ��Ŀؼ�
    m_pInstallPathEdit = static_cast<CEditUI*>(
        m_pm.FindControl(_T("install_path")));
    
    m_pLicenseCheckbox = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("license_checkbox")));
    
    m_pInstallButton = static_cast<CButtonUI*>(
        m_pm.FindControl(_T("install_button")));
    
    m_pDiskSpaceLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("disk_space_info")));
    
    // ����Ӧ�ó������ƺͰ汾����ӭҳ�棩
    CLabelUI* pAppName = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_name")));
    if (pAppName) {
        pAppName->SetText(WStringToTStr(m_config.applicationName));
    }
    
    CLabelUI* pAppVersion = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version")));
    if (pAppVersion) {
        std::wstring versionText = L"�汾 " + m_config.version;
        pAppVersion->SetText(WStringToTStr(versionText));
    }
    
    // ����Ӧ�ó������ƺͰ汾������ҳ�棩
    CLabelUI* pAppNameProgress = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_name_progress")));
    if (pAppNameProgress) {
        pAppNameProgress->SetText(WStringToTStr(m_config.applicationName));
    }
    
    CLabelUI* pAppVersionProgress = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version_progress")));
    if (pAppVersionProgress) {
        std::wstring versionText = L"�汾 " + m_config.version;
        pAppVersionProgress->SetText(WStringToTStr(versionText));
    }
    
    // ����Ӧ�ó������ƺͰ汾�����ҳ�棩
    CLabelUI* pAppNameCompletion = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_name_completion")));
    if (pAppNameCompletion) {
        pAppNameCompletion->SetText(WStringToTStr(m_config.applicationName));
    }
    
    CLabelUI* pAppVersionCompletion = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version_completion")));
    if (pAppVersionCompletion) {
        std::wstring versionText = L"�汾 " + m_config.version;
        pAppVersionCompletion->SetText(WStringToTStr(versionText));
    }
    
    // ȷ��TabLayout��ʾ��һ��ҳ�棨��ӭҳ�棩
    if (m_pTabPages) {
        m_pTabPages->SelectItem(0);
    }
}

// ����DUI��Ϣ
void GUIManager::Notify(TNotifyUI& msg) {
    // �������¼�
    if (msg.sType == _T("click")) {
        CDuiString senderName = msg.pSender->GetName();
        
        if (senderName == _T("install_button")) {
            OnInstallButtonClick();
        }
        else if (senderName == _T("cancel_button")) {
            OnCancelButtonClick();
        }
        else if (senderName == _T("browse_button")) {
            OnBrowseButtonClick();
        }
        else if (senderName == _T("finish_button")) {
            OnFinishButtonClick();
        }
        else if (senderName == _T("cancel_progress_button")) {
            OnCancelProgressButtonClick();
        }
        else if (senderName == _T("closebtn") || senderName == _T("close_button")) {
            OnCancelButtonClick();
        }
        else if (senderName == _T("minbtn")) {
            SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
        }
    }
    // �����ѡ��״̬�仯
    else if (msg.sType == _T("selectchanged")) {
        CDuiString senderName = msg.pSender->GetName();
        
        if (senderName == _T("license_checkbox")) {
            OnLicenseCheckboxChanged();
        }
    }
    // ��������ӵ��
    else if (msg.sType == _T("link")) {
        CDuiString senderName = msg.pSender->GetName();
        
        if (senderName == _T("license_link")) {
            OnLicenseLinkClick();
        }
    }
}

// ����Windows��Ϣ
LRESULT GUIManager::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_CLOSE) {
        DestroyWindow(m_hWnd);
        PostQuitMessage(0);
        return 0;
    }
    if (uMsg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    // �����Զ�����Ϣ
    if (uMsg == WM_INSTALLATION_PROGRESS) {
        ProgressMessageData* pData = reinterpret_cast<ProgressMessageData*>(lParam);
        if (pData) {
            HandleProgressMessage(pData);
            delete pData;
        }
        return 0;
    }
    else if (uMsg == WM_INSTALLATION_COMPLETE) {
        CompletionMessageData* pData = reinterpret_cast<CompletionMessageData*>(lParam);
        if (pData) {
            HandleCompletionMessage(pData);
            delete pData;
        }
        return 0;
    }
    // ���������Ϣ
    else if (uMsg == WM_KEYDOWN) {
        // ��ȡ��ǰҳ������
        int currentPage = 0;
        if (m_pTabPages) {
            currentPage = m_pTabPages->GetCurSel();
        }
        
        // ���Alt���Ƿ���
        bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        
        if (altPressed) {
            // Alt+I: ��װ��ť�����ڻ�ӭҳ�棩
            if (wParam == 'I' && currentPage == 0) {
                if (m_pInstallButton && m_pInstallButton->IsEnabled()) {
                    OnInstallButtonClick();
                    return 0;
                }
            }
            // Alt+C: ȡ����ť���ڻ�ӭҳ��ͽ���ҳ�棩
            else if (wParam == 'C' && (currentPage == 0 || currentPage == 1)) {
                if (currentPage == 0) {
                    OnCancelButtonClick();
                } else {
                    OnCancelProgressButtonClick();
                }
                return 0;
            }
            // Alt+F: ��ɰ�ť���������ҳ�棩
            else if (wParam == 'F' && currentPage == 2) {
                OnFinishButtonClick();
                return 0;
            }
        }
        else {
            // Enter��: ������ǰҳ���Ĭ�ϰ�ť
            if (wParam == VK_RETURN) {
                if (currentPage == 0) {
                    // ��ӭҳ��: ��װ��ť
                    if (m_pInstallButton && m_pInstallButton->IsEnabled()) {
                        OnInstallButtonClick();
                        return 0;
                    }
                }
                else if (currentPage == 2) {
                    // ���ҳ��: ��ɰ�ť
                    OnFinishButtonClick();
                    return 0;
                }
            }
            // Esc��: ����ȡ������
            else if (wParam == VK_ESCAPE) {
                if (currentPage == 0) {
                    OnCancelButtonClick();
                    return 0;
                }
                else if (currentPage == 1) {
                    OnCancelProgressButtonClick();
                    return 0;
                }
                else if (currentPage == 2) {
                    // ���ҳ�水Esc��ͬ�����
                    OnFinishButtonClick();
                    return 0;
                }
            }
        }
    }
    
    // ���û��ദ��
    return WindowImplBase::HandleMessage(uMsg, wParam, lParam);
}

// ��װ��ť�������
void GUIManager::OnInstallButtonClick() {
    // TODO: implement the full install flow.
    ::MessageBox(m_hWnd, _T("Installation is not implemented yet."), _T("Info"),
                 MB_OK | MB_ICONINFORMATION);
}


// ȡ����ť�������
void GUIManager::OnCancelButtonClick() {
    if (GUIHelpers::ShowConfirmDialog(
        m_hWnd,
        L"Exit",
        L"Exit the installer?")) {
        Close();
    }
}


// �����ť�������
void GUIManager::OnBrowseButtonClick() {
    // ʹ��GUIHelpers��ʾ�ļ���ѡ��Ի���
    std::wstring currentPath;
    if (m_pInstallPathEdit) {
        currentPath = m_pInstallPathEdit->GetText().GetData();
    }
    
    std::wstring selectedPath;
    if (GUIHelpers::ShowFolderBrowserDialog(
        m_hWnd,
        L"��ѡ��װĿ¼",
        currentPath,
        selectedPath)) {
        
        // ����·�������
        if (m_pInstallPathEdit) {
            m_pInstallPathEdit->SetText(WStringToTStr(selectedPath));
        }
        
        // ���´��̿ռ���Ϣ
        UpdateDiskSpaceInfo(selectedPath);
        
        // ���°�װ��ť״̬
        UpdateInstallButtonState();
    }
}

// ���Э�����ӵ������
void GUIManager::OnLicenseLinkClick() {
    ::MessageBox(m_hWnd, _T("License dialog is not implemented yet."), _T("Info"),
                 MB_OK | MB_ICONINFORMATION);
}


// ��ɰ�ť�������
void GUIManager::OnFinishButtonClick() {
    // ����Ƿ���Ҫ���Ӧ�ó���
    CCheckBoxUI* pRunAppCheckbox = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("run_app_checkbox")));
    if (pRunAppCheckbox && pRunAppCheckbox->GetCheck()) {
        // ������ִ���ļ�·��
        std::wstring installPath;
        if (m_pInstallPathEdit) {
            installPath = m_pInstallPathEdit->GetText().GetData();
        }
        
        if (!installPath.empty() && !m_config.executableName.empty()) {
            std::wstring exePath = installPath;
            if (exePath.back() != L'\\' && exePath.back() != L'/') {
                exePath += L"\\";
            }
            exePath += m_config.executableName;
            
            // ���Ӧ�ó���
            if (!GUIHelpers::LaunchApplication(exePath, installPath)) {
                GUIHelpers::ShowWarningDialog(
                    m_hWnd,
                    L"����",
                    L"�޷����Ӧ�ó������ֶ����С�");
            }
        }
    }
    
    // ����Ƿ���Ҫ����ҳ
    CCheckBoxUI* pOpenWebCheckbox = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("open_web_checkbox")));
    if (pOpenWebCheckbox && pOpenWebCheckbox->GetCheck()) {
        if (!m_config.webPageUrl.empty()) {
            // ����ҳ
            if (!GUIHelpers::OpenWebPage(m_config.webPageUrl)) {
                GUIHelpers::ShowWarningDialog(
                    m_hWnd,
                    L"����",
                    L"�޷�����ҳ�����ֶ����ʡ�");
            }
        }
    }
    
    // �رհ�װ����
    Close();
}

// ����ҳ��ȡ����ť�������
void GUIManager::OnCancelProgressButtonClick() {
    if (GUIHelpers::ShowConfirmDialog(
        m_hWnd,
        L"Cancel",
        L"Cancel installation and exit?")) {
        // TODO: request cancellation from worker.
        // if (m_pWorker) {
        //     m_pWorker->RequestCancellation();
        // }
    }
}


// ���Э�鸴ѡ��״̬�仯����
void GUIManager::OnLicenseCheckboxChanged() {
    UpdateInstallButtonState();
}

// ���°�װ��ť״̬
void GUIManager::UpdateInstallButtonState() {
    if (!m_pInstallButton || !m_pLicenseCheckbox || !m_pInstallPathEdit) {
        return;
    }
    
    // ������Э���Ƿ�ͬ��
    bool licenseAgreed = m_pLicenseCheckbox->GetCheck();
    
    // �����̿ռ��Ƿ����
    std::wstring installPath = m_pInstallPathEdit->GetText().GetData();
    uint64_t availableSpace;
    bool spaceEnough = GUIHelpers::CheckDiskSpace(
        installPath,
        m_config.requiredDiskSpace,
        availableSpace);
    
    // ֻ�е����Э��ͬ���Ҵ��̿ռ����ʱ�����ð�װ��ť
    m_pInstallButton->SetEnabled(licenseAgreed && spaceEnough);
}

// ���´��̿ռ���Ϣ
void GUIManager::UpdateDiskSpaceInfo(const std::wstring& path) {
    if (!m_pDiskSpaceLabel) {
        return;
    }
    
    // ʹ��GUIHelpers��ȡ���ô��̿ռ�
    uint64_t availableSpace = GUIHelpers::GetAvailableDiskSpace(path);
    
    // ��ʽ����ʾ�ı�
    std::wstring requiredStr = GUIHelpers::FormatBytes(m_config.requiredDiskSpace);
    std::wstring availableStr = GUIHelpers::FormatBytes(availableSpace);
    
    std::wstringstream ss;
    ss << L"����ռ�: " << requiredStr
       << L" | ���ÿռ�: " << availableStr;
    
    m_pDiskSpaceLabel->SetText(ss.str().c_str());
    
    // ����ռ䲻�㣬��ʾ������ɫ
    if (availableSpace < m_config.requiredDiskSpace) {
        m_pDiskSpaceLabel->SetTextColor(0xFFFF0000); // ��ɫ
    } else {
        m_pDiskSpaceLabel->SetTextColor(0xFF666666); // ��ɫ
    }
}

// ���������Ϣ
void GUIManager::HandleProgressMessage(ProgressMessageData* pData) {
    if (!pData) {
        return;
    }
    
    // ���µ�ǰ�ļ��б�ǩ
    CLabelUI* pCurrentFolderLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("current_folder")));
    if (pCurrentFolderLabel) {
        std::wstring folderText = L"���ڰ�װ: ";
        folderText += pData->currentFolder;
        pCurrentFolderLabel->SetText(WStringToTStr(folderText));
    }
    
    // ���½�����
    CProgressUI* pProgressBar = static_cast<CProgressUI*>(
        m_pm.FindControl(_T("progress_bar")));
    if (pProgressBar) {
        int progressValue = static_cast<int>(pData->percentage);
        pProgressBar->SetValue(progressValue);
    }
    
    // ���°ٷֱȱ�ǩ
    CLabelUI* pProgressPercentLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("progress_percent")));
    if (pProgressPercentLabel) {
        wchar_t percentText[32];
        swprintf_s(percentText, L"%.1f%%", pData->percentage);
        pProgressPercentLabel->SetText(percentText);
    }
    
    // TODO: ���㲢����Ԥ��ʣ��ʱ�䣨��Ҫ��ProgressPageController��ʵ�֣�
    // ������ʱ��ʾһ��ռλ�ı�
    CLabelUI* pEstimatedTimeLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("estimated_time")));
    if (pEstimatedTimeLabel) {
        pEstimatedTimeLabel->SetText(L"Ԥ��ʣ��ʱ��: ������...");
    }
}

// ���������Ϣ
void GUIManager::HandleCompletionMessage(CompletionMessageData* pData) {
    if (!pData) {
        return;
    }
    
    // �л������ҳ�棨ҳ������2��
    if (m_pTabPages) {
        m_pTabPages->SelectItem(2);
    }
    
    // ���½����Ϣ��ǩ
    CLabelUI* pResultMessageLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("result_message")));
    if (pResultMessageLabel) {
        if (pData->success) {
            pResultMessageLabel->SetText(L"��װ�ɹ���");
            pResultMessageLabel->SetTextColor(0xFF4CAF50); // ��ɫ
        } else {
            std::wstring errorText = L"��װʧ��: ";
            errorText += pData->errorMessage;
            pResultMessageLabel->SetText(WStringToTStr(errorText));
            pResultMessageLabel->SetTextColor(0xFFFF0000); // ��ɫ
        }
    }
    
    // �����װʧ�ܣ�����"��������"��"����ҳ"��ѡ��
    if (!pData->success) {
        CCheckBoxUI* pRunAppCheckbox = static_cast<CCheckBoxUI*>(
            m_pm.FindControl(_T("run_app_checkbox")));
        if (pRunAppCheckbox) {
            pRunAppCheckbox->SetVisible(false);
        }
        
        CCheckBoxUI* pOpenWebCheckbox = static_cast<CCheckBoxUI*>(
            m_pm.FindControl(_T("open_web_checkbox")));
        if (pOpenWebCheckbox) {
            pOpenWebCheckbox->SetVisible(false);
        }
    }
}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
