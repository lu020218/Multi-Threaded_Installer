#pragma once

#include "installer/embedded_resources.h"
#include <UIlib.h>
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
    std::string tempResourcePath;
    DuiLib::CDuiString resourcePath;
    DuiLib::CDuiString resourceBasePath;
    DuiLib::CDuiString skinsPath;
    bool useZip = false;
};

enum class GuiResourceValidationResult {
    Ok,
    Abort,
};

WindowSize GetWindowSizeFromResources(bool useZip,
                                      const DuiLib::CDuiString& resourcePath,
                                      const DuiLib::CDuiString& skinsPath,
                                      bool uninstallMode,
                                      WindowSize fallback);

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
