#pragma once

#include "installer/embedded_resources.h"
#include <UIlib.h>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

struct WindowSize {
    int width = 0;
    int height = 0;
};

struct GuiResourceContext {
    EmbeddedResourceManager resourceManager;
    std::vector<uint8_t> zipBuffer;        // RES_ZIP 原始字节，常驻内存后交给 DuiLib
    DuiLib::CDuiString resourcePath;       // 哨兵路径（非空，供相对路径推断逻辑使用）
    bool useZip = false;
};

enum class GuiResourceValidationResult {
    Ok,
    Abort,
};

// 资源全部从内存 zip 句柄读取，按 uninstall 模式选择 main 皮肤解析窗口尺寸。
WindowSize GetWindowSizeFromResources(bool uninstallMode, WindowSize fallback);

// 从当前常驻内存的资源 zip（DuiLib 缓存句柄）读取/探测单个条目。供对话框、许可证等使用。
std::string ReadActiveResourceZipEntry(const std::string& entryUtf8);
bool ActiveResourceZipHasEntry(const std::string& entryUtf8);

void PrepareGuiResources(HINSTANCE hInstance,
                         GuiResourceContext& context,
                         bool verboseLogs);

GuiResourceValidationResult ValidateInstallGuiResources(const GuiResourceContext& context);

void ApplyGuiResources(const GuiResourceContext& context, bool verboseLogs);
void LogGuiResourceDiagnostics(const GuiResourceContext& context,
                               unsigned int dpi,
                               const char* stage);
void LogActiveGuiResourceDiagnostics(unsigned int dpi, const char* stage);
void LogActiveGuiResourceDiagnosticsForXmlEntries(unsigned int dpi,
                                                 const char* stage,
                                                 const std::vector<std::string>& xmlEntries);

// Runs the heavy zip-entry-check and DPI diagnostics that were deferred from
// ApplyGuiResources so they don't block window creation.  Call once after the
// GUI message loop is running (e.g. via PostMessage / timer).
void RunDeferredGuiResourceDiagnostics();

} // namespace MultiThreadedInstaller
