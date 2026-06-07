#include "installer/platform/gui_resource_loader.h"

#include "common/installer_logger.h"
#include "gui/core/gui_helpers.h"
#include "common/utf8_utils.h"
#include "Utils/unzip.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace MultiThreadedInstaller {

using namespace DuiLib;

namespace {

struct GuiResourceDiagnosticContextSnapshot {
    bool useZip = false;
    bool valid = false;
};

GuiResourceDiagnosticContextSnapshot g_activeDiagnosticsContext;

// 哨兵资源路径：资源全部来自内存 zip，没有真实磁盘目录，但许多相对路径推断逻辑
// 仅要求 GetResourcePath() 非空且 tail≠"skins"，此值满足这些条件。
const TCHAR* const kInMemoryResourcePath = _T("memzip\\");

// 资源经 SetResourceZip(buf,len) 常驻内存，DuiLib 持有一个缓存的 HZIP 句柄。
// 所有读取都走这个句柄，不再打开任何磁盘 zip（句柄归 DuiLib 所有，切勿 CloseZip）。
HZIP ActiveResourceZip() {
    return reinterpret_cast<HZIP>(CPaintManagerUI::GetResourceZipHandle());
}

std::vector<std::string> EnumerateZipEntriesUtf8() {
    std::vector<std::string> entries;
    HZIP hz = ActiveResourceZip();
    if (hz == NULL) {
        return entries;
    }

    ZIPENTRY ze;
    if (GetZipItem(hz, -1, &ze) != 0) {
        return entries;
    }

    const int itemCount = ze.index;
    entries.reserve((std::max)(itemCount, 0));
    for (int index = 0; index < itemCount; ++index) {
        ZIPENTRY item;
        if (GetZipItem(hz, index, &item) != 0) {
            continue;
        }
        entries.push_back(WideToUtf8(TCharToWide(item.name)));
    }

    return entries;
}

std::string ReadZipEntryToString(const CDuiString& entry) {
    HZIP hz = ActiveResourceZip();
    if (hz == NULL) {
        return {};
    }
    ZIPENTRY ze;
    int index = 0;
    if (FindZipItem(hz, entry.GetData(), true, &index, &ze) != 0) {
        return {};
    }
    std::string buffer(static_cast<size_t>(ze.unc_size), '\0');
    if (UnzipItem(hz, index, buffer.data(), ze.unc_size) != 0) {
        return {};
    }
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

std::string ToLowerAsciiCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool StartsWithAscii(const std::string& value, const char* prefix) {
    if (!prefix) {
        return false;
    }
    const size_t prefixLen = std::char_traits<char>::length(prefix);
    return value.size() >= prefixLen && value.compare(0, prefixLen, prefix) == 0;
}

bool HasImageExtension(const std::string& entry) {
    const std::string lowered = ToLowerAsciiCopy(entry);
    auto endsWith = [&lowered](const char* suffix) {
        const size_t suffixLen = std::char_traits<char>::length(suffix);
        return lowered.size() >= suffixLen &&
               lowered.compare(lowered.size() - suffixLen, suffixLen, suffix) == 0;
    };
    return lowered.size() > 4 &&
           (endsWith(".png") || endsWith(".jpg") || endsWith(".jpeg") ||
            endsWith(".bmp") || endsWith(".gif") || endsWith(".webp"));
}

bool IsDigitsOnly(const std::string& text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool IsLegacyScaleText(const std::string& text) {
    if (text.size() < 2 || text.back() != 'x') {
        return false;
    }
    bool hasDigit = false;
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isdigit(c) != 0) {
            hasDigit = true;
            continue;
        }
        if (c != '.') {
            return false;
        }
    }
    return hasDigit;
}

bool HasAnyScaleSuffix(const std::string& entry) {
    const size_t dotPos = entry.find_last_of('.');
    const size_t slashPos = entry.find_last_of("/\\");
    const size_t atPos = entry.find_last_of('@');
    if (dotPos == std::string::npos || atPos == std::string::npos) {
        return false;
    }
    if (slashPos != std::string::npos && atPos < slashPos) {
        return false;
    }
    if (atPos >= dotPos) {
        return false;
    }

    const std::string suffix = entry.substr(atPos + 1, dotPos - atPos - 1);
    return IsDigitsOnly(suffix) || IsLegacyScaleText(suffix);
}

bool IsImageResourceBaseEntry(const std::string& entry) {
    if (!HasImageExtension(entry)) {
        return false;
    }
    return !HasAnyScaleSuffix(entry);
}

std::string InsertScaleSuffix(const std::string& entry, const std::string& suffix) {
    const size_t dotPos = entry.find_last_of('.');
    if (dotPos == std::string::npos) {
        return entry + suffix;
    }
    return entry.substr(0, dotPos) + suffix + entry.substr(dotPos);
}

std::string BuildExpectedScaledEntry(const std::string& entry, unsigned int scalePercent) {
    return InsertScaleSuffix(entry, "@" + std::to_string(scalePercent));
}

std::string TrimTrailingZeros(std::string text) {
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

std::string BuildLegacyScaledEntry(const std::string& entry, unsigned int scalePercent) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << (static_cast<double>(scalePercent) / 100.0);
    return InsertScaleSuffix(entry, "@" + TrimTrailingZeros(oss.str()) + "x");
}

unsigned int DpiToScalePercent(unsigned int dpi) {
    if (dpi == 0) {
        return 100;
    }
    return static_cast<unsigned int>((dpi * 100 + 48) / 96);
}

std::string JoinSampleList(const std::vector<std::string>& values, size_t limit) {
    if (values.empty()) {
        return "(none)";
    }

    std::ostringstream oss;
    const size_t count = (std::min)(values.size(), limit);
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            oss << " | ";
        }
        oss << values[i];
    }
    if (values.size() > limit) {
        oss << " | ... +" << (values.size() - limit) << " more";
    }
    return oss.str();
}

bool IsXmlEntry(const std::string& entry) {
    const std::string lowered = ToLowerAsciiCopy(entry);
    return lowered.size() >= 4 &&
           lowered.compare(lowered.size() - 4, 4, ".xml") == 0;
}

std::vector<std::string> CollectReferencedImageEntries(const std::vector<std::string>& entries,
                                                       const std::vector<std::string>* xmlEntriesFilter) {
    static const std::regex imageRefPattern(
        R"(((?:\.\./)?images[\\/][^"'<>|]+?\.(?:png|jpg|jpeg|bmp|gif|webp)))",
        std::regex::icase);

    std::unordered_set<std::string> allowedXmlEntries;
    if (xmlEntriesFilter) {
        for (const auto& xmlEntry : *xmlEntriesFilter) {
            std::string normalized = xmlEntry;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            allowedXmlEntries.insert(ToLowerAsciiCopy(normalized));
        }
    }

    std::unordered_set<std::string> uniqueRefs;
    for (const auto& entry : entries) {
        if (!IsXmlEntry(entry)) {
            continue;
        }
        if (!allowedXmlEntries.empty()) {
            std::string normalizedEntry = entry;
            std::replace(normalizedEntry.begin(), normalizedEntry.end(), '\\', '/');
            if (allowedXmlEntries.find(ToLowerAsciiCopy(normalizedEntry)) == allowedXmlEntries.end()) {
                continue;
            }
        }

        CDuiString entryPath(Utf8ToWide(entry).c_str());
        const std::string content = ReadZipEntryToString(entryPath);
        if (content.empty()) {
            continue;
        }

        for (std::sregex_iterator it(content.begin(), content.end(), imageRefPattern), end;
             it != end;
             ++it) {
            std::string match = it->str();
            std::replace(match.begin(), match.end(), '\\', '/');
            uniqueRefs.insert(match);
        }
    }

    std::vector<std::string> refs(uniqueRefs.begin(), uniqueRefs.end());
    std::sort(refs.begin(), refs.end());
    return refs;
}

void UpdateActiveDiagnosticsContext(const GuiResourceContext& context) {
    g_activeDiagnosticsContext.useZip = context.useZip;
    g_activeDiagnosticsContext.valid = true;
}

void LogZipResourceDiagnostics(unsigned int dpi,
                               const char* stage,
                               const std::vector<std::string>* xmlEntriesFilter) {
    const unsigned int scalePercent = DpiToScalePercent(dpi);
    const std::vector<std::string> entries = EnumerateZipEntriesUtf8();
    if (entries.empty()) {
        logInstallerWarning(std::string("[GUI][DPI] stage=") + (stage ? stage : "unknown") +
                            " dpi=" + std::to_string(dpi) +
                            " scale=" + std::to_string(scalePercent) +
                            "% zip_entries=0 (in-memory zip)");
        return;
    }

    std::unordered_set<std::string> entrySet(entries.begin(), entries.end());
    const std::vector<std::string> referencedImages =
        CollectReferencedImageEntries(entries, xmlEntriesFilter);
    std::vector<std::string> baseImages;
    std::vector<std::string> expectedMissing;
    std::vector<std::string> legacyFallbacks;
    std::vector<std::string> expectedPresent;
    std::vector<std::string> legacyNamedEntries;

    size_t imageEntryCount = 0;
    if (!referencedImages.empty()) {
        for (const auto& entry : referencedImages) {
            if (!HasImageExtension(entry)) {
                continue;
            }

            if (HasAnyScaleSuffix(entry) && entry.find('@') != std::string::npos &&
                entry.find('x', entry.find_last_of('@')) != std::string::npos) {
                legacyNamedEntries.push_back(entry);
            }

            if (!IsImageResourceBaseEntry(entry)) {
                continue;
            }

            baseImages.push_back(entry);
            const std::string expectedScaled = BuildExpectedScaledEntry(entry, scalePercent);
            const bool hasExpected = entrySet.find(expectedScaled) != entrySet.end();
            if (hasExpected) {
                expectedPresent.push_back(expectedScaled);
                continue;
            }

            const std::string legacyScaled = BuildLegacyScaledEntry(entry, scalePercent);
            if (entrySet.find(legacyScaled) != entrySet.end()) {
                legacyFallbacks.push_back(expectedScaled + " <= legacy exists: " + legacyScaled);
            } else {
                expectedMissing.push_back(expectedScaled);
            }
        }
    }

    for (const auto& entry : entries) {
        if (!StartsWithAscii(entry, "images/") && !StartsWithAscii(entry, "../images/")) {
            continue;
        }
        if (!HasImageExtension(entry)) {
            continue;
        }

        ++imageEntryCount;
        if (HasAnyScaleSuffix(entry) && entry.find('@') != std::string::npos &&
            entry.find('x', entry.find_last_of('@')) != std::string::npos) {
            legacyNamedEntries.push_back(entry);
        }
    }

    std::sort(legacyNamedEntries.begin(), legacyNamedEntries.end());
    legacyNamedEntries.erase(std::unique(legacyNamedEntries.begin(), legacyNamedEntries.end()),
                             legacyNamedEntries.end());

    logInstallerInfo(std::string("[GUI][DPI] stage=") + (stage ? stage : "unknown") +
                     " dpi=" + std::to_string(dpi) +
                     " scale=" + std::to_string(scalePercent) +
                     "% zip=in-memory" +
                     " xml_scope=" +
                     (xmlEntriesFilter && !xmlEntriesFilter->empty()
                          ? JoinSampleList(*xmlEntriesFilter, 6)
                          : std::string("(all-xml)")) +
                     " total_entries=" + std::to_string(entries.size()) +
                     " xml_referenced_images=" + std::to_string(referencedImages.size()) +
                     " image_entries=" + std::to_string(imageEntryCount) +
                     " base_images=" + std::to_string(baseImages.size()) +
                     " expected_scaled_present=" + std::to_string(expectedPresent.size()) +
                     " expected_scaled_missing=" + std::to_string(expectedMissing.size()) +
                     " legacy_scaled_matches=" + std::to_string(legacyFallbacks.size()) +
                     " legacy_named_entries=" + std::to_string(legacyNamedEntries.size()));

    if (!expectedMissing.empty()) {
        logInstallerWarning(std::string("[GUI][DPI] missing scaled aliases sample: ") +
                            JoinSampleList(expectedMissing, 12));
    }
    if (!legacyFallbacks.empty()) {
        logInstallerWarning(std::string("[GUI][DPI] DuiLib-style alias missing but legacy x-suffix exists: ") +
                            JoinSampleList(legacyFallbacks, 12));
    }
    if (!legacyNamedEntries.empty()) {
        logInstallerInfo(std::string("[GUI][DPI] legacy x-suffix image sample: ") +
                         JoinSampleList(legacyNamedEntries, 8));
    }
    if (!expectedPresent.empty()) {
        logInstallerDebug(std::string("[GUI][DPI] resolved scaled alias sample: ") +
                          JoinSampleList(expectedPresent, 8));
    }
}

} // namespace

WindowSize GetWindowSizeFromResources(bool uninstallMode, WindowSize fallback) {
    const wchar_t* mainFile = uninstallMode ? L"uninstall_main.xml" : L"main.xml";
    std::vector<CDuiString> candidates;
    candidates.emplace_back(CDuiString(_T("skins\\")) + mainFile);
    candidates.emplace_back(CDuiString(_T("skins/")) + mainFile);
    candidates.emplace_back(CDuiString(mainFile));

    for (const auto& entry : candidates) {
        std::string content = ReadZipEntryToString(entry);
        if (!content.empty()) {
            return ParseWindowSizeFromXml(content, fallback);
        }
    }
    return fallback;
}

void PrepareGuiResources(HINSTANCE hInstance,
                         GuiResourceContext& context,
                         bool verboseLogs) {
    CPaintManagerUI::SetInstance(hInstance);

    // 直接读取嵌入的 RES_ZIP 到内存，不再释放到临时磁盘。
    context.zipBuffer = context.resourceManager.getEmbeddedResource("RES_ZIP");
    context.useZip = !context.zipBuffer.empty();
    context.resourcePath = kInMemoryResourcePath;  // 哨兵：非空且 tail≠skins

    if (verboseLogs) {
        logInstallerInfo(std::string("[GUI][RES] Loaded in-memory resources.zip bytes=") +
                         std::to_string(context.zipBuffer.size()));
    }
}

GuiResourceValidationResult ValidateInstallGuiResources(const GuiResourceContext& context) {
    if (context.useZip && !context.zipBuffer.empty()) {
        return GuiResourceValidationResult::Ok;
    }

    CDuiString instancePath = CPaintManagerUI::GetInstancePath();
    logInstallerError(std::string("[GUI][RES] Instance path: ") +
                      WideToUtf8(TCharToWide(instancePath.GetData())));
    logInstallerError("[GUI][RES] Embedded resource zip (RES_ZIP) present: NO");

    std::wstring resourceMissingSummary =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.summary", L"");
    std::wstring debugHeader =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.debug", L"");
    std::wstring instanceLabel =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.instance_path", L"");
    std::wstring errorMessage =
        resourceMissingSummary + L"\n\n" + debugHeader + L"\n" + instanceLabel + L": " +
        TCharToWide(instancePath.GetData());

    GUIHelpers::ShowWarningDialog(
        nullptr,
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.title", L""),
        errorMessage);

    return GuiResourceValidationResult::Abort;
}

void ApplyGuiResources(const GuiResourceContext& context, bool verboseLogs) {
    UpdateActiveDiagnosticsContext(context);
    if (!context.useZip || context.zipBuffer.empty()) {
        logInstallerWarning("[GUI][RES] Resource zip enabled: false");
        logInstallerWarning("[GUI][DPI] Resource zip disabled, diagnostics skipped.");
        return;
    }

    // 哨兵资源路径（非空，供 GetLanguageFilePath 等相对路径逻辑使用）。
    CPaintManagerUI::SetResourcePath(context.resourcePath);
    // 内存加载 zip：DuiLib 会自行拷贝缓冲区并常驻一个 HZIP 句柄。
    CPaintManagerUI::SetResourceZip(const_cast<uint8_t*>(context.zipBuffer.data()),
                                    static_cast<unsigned int>(context.zipBuffer.size()));
    CPaintManagerUI::SetResourceType(UILIB_ZIP);
    if (verboseLogs) {
        logInstallerInfo(std::string("[GUI][RES] Set in-memory resource zip, bytes=") +
                         std::to_string(context.zipBuffer.size()));
    }
    logInstallerInfo("[GUI][RES] Resource zip enabled: true");
}

void LogGuiResourceDiagnostics(const GuiResourceContext& context,
                               unsigned int dpi,
                               const char* stage) {
    UpdateActiveDiagnosticsContext(context);
    if (!context.useZip) {
        logInstallerWarning(std::string("[GUI][DPI] stage=") + (stage ? stage : "unknown") +
                            " dpi=" + std::to_string(dpi) + " use_zip=false");
        return;
    }
    LogZipResourceDiagnostics(dpi, stage, nullptr);
}

void LogActiveGuiResourceDiagnostics(unsigned int dpi, const char* stage) {
    if (!g_activeDiagnosticsContext.valid || !g_activeDiagnosticsContext.useZip) {
        logInstallerWarning(std::string("[GUI][DPI] stage=") + (stage ? stage : "unknown") +
                            " dpi=" + std::to_string(dpi) + " no active in-memory zip");
        return;
    }
    LogZipResourceDiagnostics(dpi, stage, nullptr);
}

void LogActiveGuiResourceDiagnosticsForXmlEntries(unsigned int dpi,
                                                 const char* stage,
                                                 const std::vector<std::string>& xmlEntries) {
    if (!g_activeDiagnosticsContext.valid || !g_activeDiagnosticsContext.useZip) {
        logInstallerWarning(std::string("[GUI][DPI] stage=") + (stage ? stage : "unknown") +
                            " dpi=" + std::to_string(dpi) + " no active in-memory zip");
        return;
    }
    LogZipResourceDiagnostics(dpi, stage, &xmlEntries);
}

void RunDeferredGuiResourceDiagnostics() {
    if (!g_activeDiagnosticsContext.valid || !g_activeDiagnosticsContext.useZip) {
        return;
    }
    LogZipResourceDiagnostics(96, "DeferredApplyGuiResources", nullptr);
}

// 供其它模块（自定义对话框、许可证加载等）从内存 zip 句柄读取条目。
std::string ReadActiveResourceZipEntry(const std::string& entryUtf8) {
    CDuiString entry(Utf8ToWide(entryUtf8).c_str());
    return ReadZipEntryToString(entry);
}

bool ActiveResourceZipHasEntry(const std::string& entryUtf8) {
    HZIP hz = ActiveResourceZip();
    if (hz == NULL) {
        return false;
    }
    CDuiString entry(Utf8ToWide(entryUtf8).c_str());
    ZIPENTRY ze;
    int index = 0;
    return FindZipItem(hz, entry.GetData(), true, &index, &ze) == 0;
}

} // namespace MultiThreadedInstaller
