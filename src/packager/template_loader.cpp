#include "packager/template_loader.h"

#include "common/utf8_utils.h"

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

std::filesystem::path GetPackagerExecutableDirectory() {
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(modulePath).parent_path();
#else
    std::error_code ec;
    return std::filesystem::current_path(ec);
#endif
}

std::filesystem::path GetDefaultInstallerTemplatePath() {
    const std::filesystem::path exeDir = GetPackagerExecutableDirectory();
    if (exeDir.empty()) {
        return {};
    }
    return exeDir / "installer.exe";
}

std::filesystem::path GetDefaultUninstallerTemplatePath() {
    const std::filesystem::path exeDir = GetPackagerExecutableDirectory();
    if (exeDir.empty()) {
        return {};
    }
    return exeDir / "uninstaller.exe";
}

bool LoadInstallerTemplate(const std::filesystem::path& templatePath,
                           std::vector<uint8_t>& outTemplate,
                           std::string& error) {
    outTemplate.clear();
    error.clear();

    try {
        std::ifstream file(templatePath, std::ios::binary | std::ios::ate);
        if (!file) {
            error = "Failed to open installer template: " + Utf8FromPath(templatePath);
            return false;
        }

        const std::streamsize size = file.tellg();
        if (size < 0) {
            error = "Failed to determine installer template size: " + Utf8FromPath(templatePath);
            return false;
        }

        file.seekg(0, std::ios::beg);
        outTemplate.resize(static_cast<size_t>(size));
        if (size > 0 && !file.read(reinterpret_cast<char*>(outTemplate.data()), size)) {
            outTemplate.clear();
            error = "Failed to read installer template: " + Utf8FromPath(templatePath);
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        error = std::string("Error loading installer template: ") + e.what();
        outTemplate.clear();
        return false;
    }
}

} // namespace MultiThreadedInstaller
