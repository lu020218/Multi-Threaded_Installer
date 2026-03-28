#pragma once

#include "installer/embedded_resources.h"
#include <UIlib.h>
#include <string>

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
    RunConsoleFallback,
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

} // namespace MultiThreadedInstaller
