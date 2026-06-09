#pragma once

#include "installer/platform/embedded_resources.h"
#include <UIlib.h>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

/// 窗口尺寸（像素）。
struct WindowSize {
    int width = 0;
    int height = 0;
};

/// GUI 资源上下文：把内嵌 RES_ZIP 资源常驻内存并交给 DuiLib 使用。
struct GuiResourceContext {
    EmbeddedResourceManager resourceManager;  ///< 内嵌资源管理器。
    std::vector<uint8_t> zipBuffer;        ///< RES_ZIP 原始字节，常驻内存后交给 DuiLib。
    DuiLib::CDuiString resourcePath;       ///< 哨兵路径（非空，供相对路径推断逻辑使用）。
    bool useZip = false;                   ///< 是否使用内存 zip 模式。
};

/// GUI 资源校验结果。
enum class GuiResourceValidationResult {
    Ok,     ///< 资源齐全，可继续。
    Abort,  ///< 资源缺失/损坏，应中止。
};

/// 从内存 zip 解析窗口尺寸：按 uninstall 模式选择 main/uninstall_main 皮肤；解析失败用 fallback。
WindowSize GetWindowSizeFromResources(bool uninstallMode, WindowSize fallback);

/// 从当前常驻内存的资源 zip（DuiLib 缓存句柄）读取单个条目内容（供对话框、许可证等使用）。
std::string ReadActiveResourceZipEntry(const std::string& entryUtf8);
/// 探测当前内存资源 zip 是否包含某条目。
bool ActiveResourceZipHasEntry(const std::string& entryUtf8);

/// 准备 GUI 资源：从 exe 读出 RES_ZIP 到内存缓冲，填充 context。
void PrepareGuiResources(HINSTANCE hInstance,
                         GuiResourceContext& context,
                         bool verboseLogs);

/// 校验安装 GUI 所需资源是否齐全。
GuiResourceValidationResult ValidateInstallGuiResources(const GuiResourceContext& context);

/// 把 context 中的资源应用到 DuiLib（设置内存 zip 句柄等）。
void ApplyGuiResources(const GuiResourceContext& context, bool verboseLogs);
/// 输出资源诊断日志（基于给定 context）。
void LogGuiResourceDiagnostics(const GuiResourceContext& context,
                               unsigned int dpi,
                               const char* stage);
/// 输出当前生效资源的诊断日志。
void LogActiveGuiResourceDiagnostics(unsigned int dpi, const char* stage);
/// 针对指定 XML 条目输出当前资源诊断日志。
void LogActiveGuiResourceDiagnosticsForXmlEntries(unsigned int dpi,
                                                 const char* stage,
                                                 const std::vector<std::string>& xmlEntries);

/// 运行从 ApplyGuiResources 推迟的重量级 zip 条目检查与 DPI 诊断（避免阻塞建窗）。
/// 在 GUI 消息循环跑起来后调用一次（如经 PostMessage/定时器）。
void RunDeferredGuiResourceDiagnostics();

} // namespace MultiThreadedInstaller
