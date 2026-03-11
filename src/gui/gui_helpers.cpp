#include "../../include/gui/gui_helpers.h"
#include "../../include/gui/message_box_dialog.h"
#include "common/utf8_utils.h"
#include <UIlib.h>
#include "Utils/unzip.h"
#include <shlobj.h>
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace MultiThreadedInstaller {
using namespace DuiLib;

namespace {
bool CanUseCustomDialog() {
    if (CPaintManagerUI::GetResourcePath().IsEmpty()) {
        return false;
    }

    if (CPaintManagerUI::GetResourceType() == UILIB_ZIP) {
        CDuiString zipName = CPaintManagerUI::GetResourceZip();
        if (zipName.IsEmpty()) {
            return false;
        }

        CDuiString resourcePath = CPaintManagerUI::GetResourcePath();
        CDuiString zipPathText = resourcePath + zipName;
        std::filesystem::path zipPath = PathFromTChar(zipPathText.GetData());
        if (!std::filesystem::exists(zipPath)) {
            return false;
        }

        HZIP hz = OpenZip(zipPathText.GetData(), 0);
        if (hz == NULL) {
            return false;
        }
        ZIPENTRY ze;
        int index = 0;
        bool found = (FindZipItem(hz, _T("skins/msgBox.xml"), true, &index, &ze) == 0);
        if (!found) {
            found = (FindZipItem(hz, _T("skins\\msgBox.xml"), true, &index, &ze) == 0);
        }
        if (!found) {
            found = (FindZipItem(hz, _T("msgBox.xml"), true, &index, &ze) == 0);
        }
        CloseZip(hz);
        return found;
    }

    std::filesystem::path skinPath = PathFromTChar(CPaintManagerUI::GetResourcePath().GetData());
    skinPath /= L"skins";
    skinPath /= L"msgBox.xml";
    return std::filesystem::exists(skinPath);
}
DialogResult ShowFallbackMessageBox(HWND hParent,
                                    const std::wstring& title,
                                    const std::wstring& message,
                                    UINT flags) {
    int result = MessageBoxW(hParent, message.c_str(), title.c_str(), flags);
    if ((flags & MB_YESNOCANCEL) == MB_YESNOCANCEL) {
        if (result == IDYES) {
            return DialogResult::Ok;
        }
        if (result == IDNO) {
            return DialogResult::Cancel;
        }
        return DialogResult::Alt;
    }
    if ((flags & MB_YESNO) == MB_YESNO) {
        return result == IDYES ? DialogResult::Ok : DialogResult::Cancel;
    }
    return DialogResult::Ok;
}

DialogResult ShowCustomDialogInternal(HWND hParent,
                                      const std::wstring& title,
                                      const std::wstring& message,
                                      const std::wstring& okText,
                                      const std::wstring& cancelText,
                                      const std::wstring& altText,
                                      UINT fallbackFlags) {
    if (CanUseCustomDialog()) {
        MessageBoxDialog dialog(title, message, okText, cancelText, altText);
        return dialog.ShowModal(hParent);
    }
    return ShowFallbackMessageBox(hParent, title, message, fallbackFlags);
}
} // namespace


static bool g_comInitialized = false;

// ============================================================================

// ============================================================================

bool GUIHelpers::ShowFolderBrowserDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& initialPath,
    std::wstring& selectedPath) {
    

    bool needUninitialize = false;
    if (!g_comInitialized) {
        if (!InitializeCOM()) {
            return false;
        }
        needUninitialize = true;
    }
    

    BROWSEINFO bi = { 0 };
    bi.hwndOwner = hParent;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    

    if (!initialPath.empty()) {
        bi.lParam = reinterpret_cast<LPARAM>(initialPath.c_str());
        bi.lpfn = [](HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) -> int {
            if (uMsg == BFFM_INITIALIZED && lpData != 0) {
                const wchar_t* path = reinterpret_cast<const wchar_t*>(lpData);
                SendMessage(hwnd, BFFM_SETSELECTION, TRUE, reinterpret_cast<LPARAM>(path));
            }
            return 0;
        };
    }
    

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    bool result = false;
    
    if (pidl != nullptr) {

        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDList(pidl, path)) {
            selectedPath = path;
            result = true;
        }
        

        CoTaskMemFree(pidl);
    }
    

    if (needUninitialize) {
        UninitializeCOM();
    }
    
    return result;
}

// ============================================================================

// ============================================================================

uint64_t GUIHelpers::GetAvailableDiskSpace(const std::wstring& path) {
    if (path.empty()) {
        return 0;
    }
    

    std::wstring rootPath = ExtractRootPath(path);
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

uint64_t GUIHelpers::GetTotalDiskSpace(const std::wstring& path) {
    if (path.empty()) {
        return 0;
    }

    std::wstring rootPath = ExtractRootPath(path);
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
        return totalNumberOfBytes.QuadPart;
    }

    return 0;
}

std::wstring GUIHelpers::FormatBytes(uint64_t bytes) {
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

bool GUIHelpers::CheckDiskSpace(
    const std::wstring& path,
    uint64_t requiredBytes,
    uint64_t& availableBytes) {
    
    availableBytes = GetAvailableDiskSpace(path);
    return availableBytes >= requiredBytes;
}

// ============================================================================

// ============================================================================

bool GUIHelpers::LaunchApplication(
    const std::wstring& executablePath,
    const std::wstring& workingDirectory) {
    
    if (executablePath.empty()) {
        return false;
    }
    

    if (!std::filesystem::exists(executablePath)) {
        return false;
    }
    

    std::wstring workDir = workingDirectory;
    if (workDir.empty()) {

        std::filesystem::path exePath(executablePath);
        workDir = exePath.parent_path().wstring();
    }
    

    HINSTANCE result = ShellExecuteW(
        NULL,
        L"open",
        executablePath.c_str(),
        NULL,
        workDir.empty() ? NULL : workDir.c_str(),
        SW_SHOWNORMAL
    );
    

    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool GUIHelpers::OpenWebPage(const std::wstring& url) {
    if (url.empty()) {
        return false;
    }
    

    if (url.find(L"http://") != 0 && url.find(L"https://") != 0) {
        return false;
    }
    

    HINSTANCE result = ShellExecuteW(
        NULL,
        L"open",
        url.c_str(),    // URL
        NULL,
        NULL,
        SW_SHOWNORMAL
    );
    

    return reinterpret_cast<INT_PTR>(result) > 32;
}

// ============================================================================

// ============================================================================

void GUIHelpers::ShowErrorDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& message) {
    std::wstring okText = GetLocalizedText(L"msg.msgbox.ok", L"");
    ShowCustomDialogInternal(hParent, title, message, okText, L"", L"",
                             MB_OK | MB_ICONERROR);
}

void GUIHelpers::ShowWarningDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& message) {
    std::wstring okText = GetLocalizedText(L"msg.msgbox.ok", L"");
    ShowCustomDialogInternal(hParent, title, message, okText, L"", L"",
                             MB_OK | MB_ICONWARNING);
}

bool GUIHelpers::ShowConfirmDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& message) {
    std::wstring okText = GetLocalizedText(L"msg.msgbox.ok", L"");
    std::wstring cancelText = GetLocalizedText(L"msg.msgbox.cancel", L"");
    DialogResult result = ShowCustomDialogInternal(hParent, title, message,
                                                   okText, cancelText, L"",
                                                   MB_YESNO | MB_ICONQUESTION);
    return result == DialogResult::Ok;
}

void GUIHelpers::ShowInfoDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& message) {
    std::wstring okText = GetLocalizedText(L"msg.msgbox.ok", L"");
    ShowCustomDialogInternal(hParent, title, message, okText, L"", L"",
                             MB_OK | MB_ICONINFORMATION);
}

DialogResult GUIHelpers::ShowCustomDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& message,
    const std::wstring& okText,
    const std::wstring& cancelText,
    const std::wstring& altText) {

    std::wstring resolvedOk = okText;
    std::wstring resolvedCancel = cancelText;
    std::wstring resolvedAlt = altText;
    if (resolvedOk.empty()) {
        resolvedOk = GetLocalizedText(L"msg.msgbox.ok", L"");
    }
    if (resolvedCancel.empty() && !cancelText.empty()) {
        resolvedCancel = GetLocalizedText(L"msg.msgbox.cancel", L"");
    }
    if (resolvedAlt.empty() && !altText.empty()) {
        resolvedAlt = GetLocalizedText(L"msg.msgbox.cancel", L"");
    }

    UINT flags = MB_OK | MB_ICONINFORMATION;
    if (!resolvedCancel.empty() && resolvedAlt.empty()) {
        flags = MB_YESNO | MB_ICONQUESTION;
    } else if (!resolvedCancel.empty() && !resolvedAlt.empty()) {
        flags = MB_YESNOCANCEL | MB_ICONWARNING;
    }
    return ShowCustomDialogInternal(hParent, title, message, resolvedOk, resolvedCancel, resolvedAlt, flags);
}

std::wstring GUIHelpers::GetUILanguageCode() {
    LPCTSTR lang = CResourceManager::GetInstance()->GetLanguage();
    if (!lang) {
        return L"";
    }
    return std::wstring(lang);
}

std::wstring GUIHelpers::GetLocalizedText(const std::wstring& textId,
                                          const std::wstring& fallback) {
    if (textId.empty()) {
        return fallback;
    }
    CDuiString text = CResourceManager::GetInstance()->GetText(textId.c_str());
    if (text.IsEmpty()) {
        return fallback;
    }
    return std::wstring(text.GetData());
}

// ============================================================================

// ============================================================================

bool GUIHelpers::InitializeCOM() {
    if (g_comInitialized) {
        return true;
    }
    

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    



    if (hr == S_OK || hr == S_FALSE) {
        g_comInitialized = true;
        return true;
    }
    

    if (hr == RPC_E_CHANGED_MODE) {
        return true;
    }
    
    return false;
}

void GUIHelpers::UninitializeCOM() {
    if (g_comInitialized) {
        CoUninitialize();
        g_comInitialized = false;
    }
}

bool GUIHelpers::ValidatePath(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    


    if (path.length() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {

        wchar_t drive = path[0];
        if ((drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z')) {
            return true;
        }
    }
    

    if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        return true;
    }
    

    const wchar_t* invalidChars = L"<>:\"|?*";

    size_t startPos = (path.length() >= 2 && path[1] == L':') ? 2 : 0;
    if (path.find_first_of(invalidChars, startPos) != std::wstring::npos) {
        return false;
    }
    
    return false;
}

std::wstring GUIHelpers::ExtractRootPath(const std::wstring& path) {
    if (path.empty()) {
        return L"";
    }
    

    if (path.length() >= 2 && path[1] == L':') {
        wchar_t drive = path[0];
        if ((drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z')) {
            return path.substr(0, 2) + L"\\";
        }
    }
    

    if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\') {

        size_t pos = path.find(L'\\', 2);
        if (pos != std::wstring::npos) {

            pos = path.find(L'\\', pos + 1);
            if (pos != std::wstring::npos) {
                return path.substr(0, pos + 1);
            } else {

                return path + L"\\";
            }
        }
    }
    

    return L"C:\\";
}

} // namespace MultiThreadedInstaller

