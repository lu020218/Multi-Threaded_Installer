#include "../../include/gui/gui_helpers.h"
#include "../../include/gui/message_box_dialog.h"
#include "installer/gui_resource_loader.h"
#include "common/utf8_utils.h"
#include <UIlib.h>
#include "Utils/unzip.h"
#include <shlobj.h>
#include <shellapi.h>
#include <cwctype>
#include <map>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <vector>

namespace MultiThreadedInstaller {
using namespace DuiLib;

namespace {
bool CanUseCustomDialog() {
    // 资源经 UILIB_ZIP 常驻内存，直接从 DuiLib 缓存句柄探测皮肤条目是否存在。
    if (CPaintManagerUI::GetResourceType() != UILIB_ZIP) {
        return false;
    }
    return ActiveResourceZipHasEntry("skins/msgBox.xml") ||
           ActiveResourceZipHasEntry("skins\\msgBox.xml") ||
           ActiveResourceZipHasEntry("msgBox.xml");
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

std::wstring NormalizeEnvironmentKey(const std::wstring& key) {
    std::wstring normalized = key;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    return normalized;
}

std::vector<wchar_t> BuildEnvironmentBlock(
    const std::vector<std::pair<std::wstring, std::wstring>>& environment) {
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> merged;

    LPWCH currentEnv = GetEnvironmentStringsW();
    if (currentEnv) {
        for (LPWCH current = currentEnv; *current; ) {
            std::wstring entry = current;
            size_t separator = entry.find(L'=');
            if (separator != std::wstring::npos && separator > 0) {
                std::wstring key = entry.substr(0, separator);
                std::wstring value = entry.substr(separator + 1);
                merged[NormalizeEnvironmentKey(key)] = { key, value };
            }
            current += entry.size() + 1;
        }
        FreeEnvironmentStringsW(currentEnv);
    }

    for (const auto& item : environment) {
        if (item.first.empty()) {
            continue;
        }
        merged[NormalizeEnvironmentKey(item.first)] = item;
    }

    std::vector<wchar_t> block;
    for (const auto& item : merged) {
        const std::wstring& key = item.second.first;
        const std::wstring& value = item.second.second;
        block.insert(block.end(), key.begin(), key.end());
        block.push_back(L'=');
        block.insert(block.end(), value.begin(), value.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
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

bool GUIHelpers::LaunchApplicationWithEnvironment(
    const std::wstring& executablePath,
    const std::wstring& workingDirectory,
    const std::vector<std::pair<std::wstring, std::wstring>>& environment) {

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

    std::wstring commandLine = L"\"" + executablePath + L"\"";
    std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back(L'\0');
    std::vector<wchar_t> envBlock = BuildEnvironmentBlock(environment);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    BOOL started = CreateProcessW(nullptr,
                                  commandLineBuffer.data(),
                                  nullptr,
                                  nullptr,
                                  FALSE,
                                  CREATE_UNICODE_ENVIRONMENT,
                                  envBlock.data(),
                                  workDir.empty() ? nullptr : workDir.c_str(),
                                  &startupInfo,
                                  &processInfo);
    if (!started) {
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
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
    std::wstring resolved(text.GetData());
    if (resolved == textId) {
        return fallback;
    }
    return resolved;
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

