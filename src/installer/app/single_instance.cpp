#include "installer/app/single_instance.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"

#ifdef _WIN32

#include <cstdint>
#include <cstring>

namespace MultiThreadedInstaller {
namespace {

// 把任意 key 归一化为合法的内核对象名片段（去掉路径分隔符与命名空间分隔符）。
std::wstring SanitizeKey(const std::string& key) {
    std::wstring wide = Utf8ToWide(key);
    for (wchar_t& c : wide) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|') {
            c = L'_';
        }
    }
    return wide;
}

// 互斥量/共享内存均放在会话级命名空间 Local\，即“同一登录会话内单例”。
std::wstring MutexNameFor(const std::string& key) {
    return L"Local\\MTInstaller_" + SanitizeKey(key) + L"_singleton_mutex";
}
std::wstring MappingNameFor(const std::string& key) {
    return L"Local\\MTInstaller_" + SanitizeKey(key) + L"_window";
}

// GUI 窗口句柄共享内存——在 GUI 运行期间须保持打开，否则命名对象会被销毁。
HANDLE g_windowMapping = nullptr;

} // namespace

SingleInstanceGuard::SingleInstanceGuard(const std::string& instanceKey) : key_(instanceKey) {
    const std::wstring name = MutexNameFor(key_);
    mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (mutex_ == nullptr) {
        // 创建失败时保守放行（不阻断安装），但记日志便于排查。
        acquired_ = true;
        logInstallerWarning("[SingleInstance] CreateMutex failed; proceeding without single-instance guard. key=" +
                            key_);
        return;
    }
    acquired_ = (GetLastError() != ERROR_ALREADY_EXISTS);
}

SingleInstanceGuard::~SingleInstanceGuard() {
    if (mutex_ != nullptr) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstanceGuard::activateExistingWindow() const {
    // 打开已有实例发布的窗口句柄共享内存；打不开 → 已有实例为静默（或窗口尚未就绪）。
    const std::wstring name = MappingNameFor(key_);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    if (mapping == nullptr) {
        logInstallerInfo("[SingleInstance] Existing instance is not GUI (no published window).");
        return false;
    }

    HWND hwnd = nullptr;
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(uint64_t));
    if (view != nullptr) {
        uint64_t raw = 0;
        std::memcpy(&raw, view, sizeof(raw));
        hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(raw));
        UnmapViewOfFile(view);
    }
    CloseHandle(mapping);

    if (hwnd != nullptr && IsWindow(hwnd)) {
        if (IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        } else {
            ShowWindow(hwnd, SW_SHOW);
        }
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        logInstallerInfo("[SingleInstance] Existing GUI instance found; brought its window to front.");
        return true;
    }
    logInstallerInfo("[SingleInstance] Published window is no longer valid (existing instance not GUI).");
    return false;
}

void PublishGuiInstanceWindow(const std::string& instanceKey, HWND hwnd) {
    const std::wstring name = MappingNameFor(instanceKey);
    g_windowMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                         sizeof(uint64_t), name.c_str());
    if (g_windowMapping == nullptr) {
        logInstallerWarning("[SingleInstance] Failed to create window mapping; "
                            "second instance won't be able to focus this window. key=" + instanceKey);
        return;
    }
    void* view = MapViewOfFile(g_windowMapping, FILE_MAP_WRITE, 0, 0, sizeof(uint64_t));
    if (view != nullptr) {
        const uint64_t raw = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd));
        std::memcpy(view, &raw, sizeof(raw));
        UnmapViewOfFile(view);
        logInstallerInfo("[SingleInstance] Published GUI window handle for key=" + instanceKey);
    }
}

void ClearGuiInstanceWindow() {
    if (g_windowMapping != nullptr) {
        CloseHandle(g_windowMapping);  // 关闭后命名共享内存销毁，其它实例将其视为“非 GUI”。
        g_windowMapping = nullptr;
    }
}

} // namespace MultiThreadedInstaller

#else  // !_WIN32

namespace MultiThreadedInstaller {

SingleInstanceGuard::SingleInstanceGuard(const std::string& instanceKey) : key_(instanceKey) {
    acquired_ = true;  // 非 Windows 平台不做单例（项目当前仅构建 Windows）。
}
SingleInstanceGuard::~SingleInstanceGuard() = default;
bool SingleInstanceGuard::activateExistingWindow() const { return false; }

} // namespace MultiThreadedInstaller

#endif // _WIN32
