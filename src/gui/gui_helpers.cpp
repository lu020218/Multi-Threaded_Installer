#ifdef GUI_ENABLED

#include "../../include/gui/gui_helpers.h"
#include <shlobj.h>
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace MultiThreadedInstaller {

// 静态变量：跟踪COM初始化状态
static bool g_comInitialized = false;

// ============================================================================
// 8.1 文件浏览对话框
// ============================================================================

bool GUIHelpers::ShowFolderBrowserDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& initialPath,
    std::wstring& selectedPath) {
    
    // 确保COM已初始化
    bool needUninitialize = false;
    if (!g_comInitialized) {
        if (!InitializeCOM()) {
            return false;
        }
        needUninitialize = true;
    }
    
    // 使用SHBrowseForFolder显示文件夹选择对话框
    BROWSEINFO bi = { 0 };
    bi.hwndOwner = hParent;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    
    // 设置初始路径（如果提供）
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
    
    // 显示对话框
    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    bool result = false;
    
    if (pidl != nullptr) {
        // 获取选择的路径
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDList(pidl, path)) {
            selectedPath = path;
            result = true;
        }
        
        // 释放PIDL
        CoTaskMemFree(pidl);
    }
    
    // 如果是临时初始化的COM，需要反初始化
    if (needUninitialize) {
        UninitializeCOM();
    }
    
    return result;
}

// ============================================================================
// 8.2 磁盘空间查询
// ============================================================================

uint64_t GUIHelpers::GetAvailableDiskSpace(const std::wstring& path) {
    if (path.empty()) {
        return 0;
    }
    
    // 提取驱动器根路径
    std::wstring rootPath = ExtractRootPath(path);
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
// 8.3 应用程序启动和网页打开
// ============================================================================

bool GUIHelpers::LaunchApplication(
    const std::wstring& executablePath,
    const std::wstring& workingDirectory) {
    
    if (executablePath.empty()) {
        return false;
    }
    
    // 验证可执行文件是否存在
    if (!std::filesystem::exists(executablePath)) {
        return false;
    }
    
    // 确定工作目录
    std::wstring workDir = workingDirectory;
    if (workDir.empty()) {
        // 使用可执行文件所在目录作为工作目录
        std::filesystem::path exePath(executablePath);
        workDir = exePath.parent_path().wstring();
    }
    
    // 使用ShellExecute启动应用程序
    HINSTANCE result = ShellExecuteW(
        NULL,                       // 父窗口句柄
        L"open",                    // 操作
        executablePath.c_str(),     // 文件路径
        NULL,                       // 参数
        workDir.empty() ? NULL : workDir.c_str(),  // 工作目录
        SW_SHOWNORMAL               // 显示方式
    );
    
    // ShellExecute返回值大于32表示成功
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool GUIHelpers::OpenWebPage(const std::wstring& url) {
    if (url.empty()) {
        return false;
    }
    
    // 验证URL格式（简单检查）
    if (url.find(L"http://") != 0 && url.find(L"https://") != 0) {
        return false;
    }
    
    // 使用ShellExecute在默认浏览器中打开URL
    HINSTANCE result = ShellExecuteW(
        NULL,           // 父窗口句柄
        L"open",        // 操作
        url.c_str(),    // URL
        NULL,           // 参数
        NULL,           // 工作目录
        SW_SHOWNORMAL   // 显示方式
    );
    
    // ShellExecute返回值大于32表示成功
    return reinterpret_cast<INT_PTR>(result) > 32;
}

// ============================================================================
// 8.4 错误处理和对话框
// ============================================================================

void GUIHelpers::ShowErrorDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& message) {
    
    MessageBoxW(
        hParent,
        message.c_str(),
        title.c_str(),
        MB_OK | MB_ICONERROR
    );
}

void GUIHelpers::ShowWarningDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& message) {
    
    MessageBoxW(
        hParent,
        message.c_str(),
        title.c_str(),
        MB_OK | MB_ICONWARNING
    );
}

bool GUIHelpers::ShowConfirmDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& message) {
    
    int result = MessageBoxW(
        hParent,
        message.c_str(),
        title.c_str(),
        MB_YESNO | MB_ICONQUESTION
    );
    
    return (result == IDYES);
}

void GUIHelpers::ShowInfoDialog(
    HWND hParent,
    const std::wstring& title,
    const std::wstring& message) {
    
    MessageBoxW(
        hParent,
        message.c_str(),
        title.c_str(),
        MB_OK | MB_ICONINFORMATION
    );
}

// ============================================================================
// 辅助函数
// ============================================================================

bool GUIHelpers::InitializeCOM() {
    if (g_comInitialized) {
        return true;
    }
    
    // 初始化COM库（单线程模式）
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    
    // S_OK表示成功初始化
    // S_FALSE表示COM已经被初始化（在同一线程中）
    // RPC_E_CHANGED_MODE表示COM已经以不同模式初始化
    if (hr == S_OK || hr == S_FALSE) {
        g_comInitialized = true;
        return true;
    }
    
    // 如果COM已经以不同模式初始化，我们仍然可以使用它
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
    
    // 检查路径格式
    // 1. 绝对路径（C:\...）
    if (path.length() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
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
    
    // 3. 检查是否包含非法字符
    const wchar_t* invalidChars = L"<>:\"|?*";
    // 跳过驱动器字母后的冒号
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
    
    // 1. 处理绝对路径（C:\...）
    if (path.length() >= 2 && path[1] == L':') {
        wchar_t drive = path[0];
        if ((drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z')) {
            return path.substr(0, 2) + L"\\";
        }
    }
    
    // 2. 处理UNC路径（\\server\share\...）
    if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        // 查找服务器名称后的第一个反斜杠
        size_t pos = path.find(L'\\', 2);
        if (pos != std::wstring::npos) {
            // 查找共享名称后的第一个反斜杠
            pos = path.find(L'\\', pos + 1);
            if (pos != std::wstring::npos) {
                return path.substr(0, pos + 1);
            } else {
                // 如果没有更多的反斜杠，返回整个路径加上反斜杠
                return path + L"\\";
            }
        }
    }
    
    // 3. 默认返回C盘
    return L"C:\\";
}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
