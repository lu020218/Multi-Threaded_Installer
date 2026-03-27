#include "installer/gui_resource_loader.h"

#include "gui/gui_helpers.h"
#include "common/utf8_utils.h"
#include "Utils/unzip.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace MultiThreadedInstaller {

using namespace DuiLib;

namespace {

std::string ReadFileToString(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

bool ZipEntryExists(const CDuiString& zipPath, const CDuiString& entry) {
    HZIP hz = OpenZip(zipPath.GetData(), 0);
    if (hz == NULL) {
        return false;
    }
    ZIPENTRY ze;
    int index = 0;
    bool found = (FindZipItem(hz, entry.GetData(), true, &index, &ze) == 0);
    CloseZip(hz);
    return found;
}

void LogZipEntryCheck(const CDuiString& zipPath, const std::vector<CDuiString>& entries) {
    std::string zipPathUtf8 = WideToUtf8(TCharToWide(zipPath.GetData()));
    if (!zipPathUtf8.empty()) {
        std::cout << "Resource zip path: " << zipPathUtf8 << std::endl;
    }
    for (const auto& entry : entries) {
        std::string entryUtf8 = WideToUtf8(TCharToWide(entry.GetData()));
        std::cout << "Zip entry check: " << entryUtf8 << " -> "
                  << (ZipEntryExists(zipPath, entry) ? "found" : "missing")
                  << std::endl;
    }
}

std::string ReadZipEntryToString(const CDuiString& zipPath, const CDuiString& entry) {
    HZIP hz = OpenZip(zipPath.GetData(), 0);
    if (hz == NULL) {
        return {};
    }
    ZIPENTRY ze;
    int index = 0;
    if (FindZipItem(hz, entry.GetData(), true, &index, &ze) != 0) {
        CloseZip(hz);
        return {};
    }
    std::string buffer(static_cast<size_t>(ze.unc_size), '\0');
    if (UnzipItem(hz, index, buffer.data(), ze.unc_size) != 0) {
        CloseZip(hz);
        return {};
    }
    CloseZip(hz);
    return buffer;
}

WindowSize ParseWindowSizeFromXml(const std::string& xml, WindowSize fallback) {
    size_t pos = xml.find("size=\"");
    if (pos == std::string::npos) {
        pos = xml.find("size='");
    }
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = xml.find_first_of("\"'", pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    char quote = xml[pos];
    size_t end = xml.find(quote, pos + 1);
    if (end == std::string::npos) {
        return fallback;
    }
    std::string sizeText = xml.substr(pos + 1, end - pos - 1);
    size_t comma = sizeText.find(',');
    if (comma == std::string::npos) {
        return fallback;
    }
    try {
        int w = std::stoi(sizeText.substr(0, comma));
        int h = std::stoi(sizeText.substr(comma + 1));
        if (w > 0 && h > 0) {
            return WindowSize{ w, h };
        }
    } catch (...) {
        return fallback;
    }
    return fallback;
}

std::vector<CDuiString> BuildResourceZipChecks() {
    std::vector<CDuiString> checks;
    checks.emplace_back(_T("images/bg2.png"));
    checks.emplace_back(_T("../images/bg2.png"));
    checks.emplace_back(_T("images/bg2@150.png"));
    checks.emplace_back(_T("../images/bg2@150.png"));
    checks.emplace_back(_T("images/bg2@200.png"));
    checks.emplace_back(_T("../images/bg2@200.png"));
    checks.emplace_back(_T("images/logo3.png"));
    checks.emplace_back(_T("../images/logo3.png"));
    checks.emplace_back(_T("skins/msgBox.xml"));
    checks.emplace_back(_T("skins\\msgBox.xml"));
    checks.emplace_back(_T("msgBox.xml"));
    return checks;
}

} // namespace

WindowSize GetWindowSizeFromResources(bool useZip,
                                      const CDuiString& resourcePath,
                                      const CDuiString& skinsPath,
                                      bool uninstallMode,
                                      WindowSize fallback) {
    const wchar_t* zipFileName = L"resources.zip";
    const wchar_t* mainFile = uninstallMode ? L"uninstall_main.xml" : L"main.xml";

    if (useZip) {
        CDuiString zipPath = resourcePath + zipFileName;
        std::vector<CDuiString> candidates;
        candidates.emplace_back(CDuiString(_T("skins\\")) + mainFile);
        candidates.emplace_back(CDuiString(_T("skins/")) + mainFile);
        candidates.emplace_back(CDuiString(mainFile));

        for (const auto& entry : candidates) {
            std::string content = ReadZipEntryToString(zipPath, entry);
            if (!content.empty()) {
                return ParseWindowSizeFromXml(content, fallback);
            }
        }
        return fallback;
    }

    std::filesystem::path filePath = PathFromTChar(skinsPath.GetData());
    filePath /= mainFile;
    std::string content = ReadFileToString(filePath);
    if (content.empty()) {
        return fallback;
    }
    return ParseWindowSizeFromXml(content, fallback);
}

void PrepareGuiResources(HINSTANCE hInstance,
                         GuiResourceContext& context,
                         bool verboseLogs) {
    context.tempResourcePath = context.resourceManager.extractResources();
    CPaintManagerUI::SetInstance(hInstance);

    if (!context.tempResourcePath.empty()) {
#if defined(UNICODE) || defined(_UNICODE)
        std::wstring wpath = Utf8ToWide(context.tempResourcePath);
        if (!wpath.empty()) {
            context.resourceBasePath = wpath.c_str();
        }
#else
        context.resourceBasePath = context.tempResourcePath.c_str();
#endif
        context.resourcePath = context.resourceBasePath;
        if (!context.resourcePath.IsEmpty()) {
            TCHAR lastChar = context.resourcePath.GetAt(context.resourcePath.GetLength() - 1);
            if (lastChar != _T('\\') && lastChar != _T('/')) {
                context.resourcePath += _T("\\");
            }
        }
        context.skinsPath = context.resourcePath + _T("skins\\");
        if (verboseLogs) {
            std::cout << "Using extracted resources from: " << context.tempResourcePath << std::endl;
        }
    }

    context.useZip = false;
    if (!context.tempResourcePath.empty()) {
        std::filesystem::path zipPath = PathFromUtf8(context.tempResourcePath) / "resources.zip";
        context.useZip = std::filesystem::exists(zipPath);
    }
}

GuiResourceValidationResult ValidateInstallGuiResources(const GuiResourceContext& context) {
    if (!context.tempResourcePath.empty() && context.useZip && !context.resourcePath.IsEmpty()) {
        return GuiResourceValidationResult::Ok;
    }

    CDuiString instancePath = CPaintManagerUI::GetInstancePath();
    std::wcout << L"Instance path: " << instancePath.GetData() << std::endl;
    std::wcout << L"Resource path: " << context.resourcePath.GetData() << std::endl;
    std::wcout << L"Skin path: " << context.skinsPath.GetData() << std::endl;
    std::wcout << L"Embedded resource temp path present: "
               << (!context.tempResourcePath.empty() ? L"YES" : L"NO") << std::endl;
    std::wcout << L"Embedded resource zip present: " << (context.useZip ? L"YES" : L"NO")
               << std::endl;

    std::wstring resourceMissingSummary =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.summary", L"");
    std::wstring debugHeader =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.debug", L"");
    std::wstring instanceLabel =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.instance_path", L"");
    std::wstring resourceLabel =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.resource_path", L"");
    std::wstring errorMessage =
        resourceMissingSummary + L"\n\n" + debugHeader + L"\n" + instanceLabel + L": " +
        TCharToWide(instancePath.GetData()) + L"\n" + resourceLabel + L": " +
        TCharToWide(context.resourceBasePath.GetData());

    GUIHelpers::ShowWarningDialog(
        nullptr,
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.title", L""),
        errorMessage);

    bool debugMode = GetEnvironmentVariableW(L"MTINSTALLER_DEBUG", nullptr, 0) > 0;
    return debugMode ? GuiResourceValidationResult::RunConsoleFallback
                     : GuiResourceValidationResult::Abort;
}

void ApplyGuiResources(const GuiResourceContext& context, bool verboseLogs) {
    if (!context.useZip || context.resourcePath.IsEmpty()) {
        std::cout << "Resource zip enabled: false" << std::endl;
        return;
    }

    CPaintManagerUI::SetResourcePath(context.resourcePath);
    CPaintManagerUI::SetResourceZip(_T("resources.zip"), true);
    CPaintManagerUI::SetResourceType(UILIB_ZIP);
    if (verboseLogs) {
        std::wcout << L"Set resource zip to: " << context.resourcePath.GetData()
                   << L"resources.zip" << std::endl;
    }
    std::cout << "Resource zip enabled: true" << std::endl;
    CDuiString zipPath = context.resourcePath + _T("resources.zip");
    LogZipEntryCheck(zipPath, BuildResourceZipChecks());
}

} // namespace MultiThreadedInstaller
