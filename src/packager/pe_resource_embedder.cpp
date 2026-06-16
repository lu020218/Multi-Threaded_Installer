#include "packager/pe_resource_embedder.h"

#include "packager/resource_zip_builder.h"
#include "common/utf8_utils.h"

#include <Windows.h>
#include <fstream>
#include <utility>
#include <vector>

namespace MultiThreadedInstaller {
namespace {

bool ReadAllBytes(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "open failed: " + Utf8FromPath(path);
        return false;
    }
    const std::streamoff size = file.tellg();
    out.resize(static_cast<size_t>(size > 0 ? size : 0));
    file.seekg(0, std::ios::beg);
    if (size > 0) {
        file.read(reinterpret_cast<char*>(out.data()), size);
    }
    if (!file && size > 0) {
        error = "read failed: " + Utf8FromPath(path);
        return false;
    }
    return true;
}

// Inject one or more BINARY-typed PE resources into exePath in a single update session.
// Existing resources (icon / version info / manifest applied earlier) are preserved.
// Type and names are uppercase strings so the runtime's FindResourceA(name, "BINARY") matches.
bool UpdateBinaryResources(const std::filesystem::path& exePath,
                           const std::vector<std::pair<std::string, const std::vector<uint8_t>*>>& entries,
                           std::string& error) {
    const std::wstring wpath = exePath.wstring();
    HANDLE update = BeginUpdateResourceW(wpath.c_str(), FALSE);  // FALSE: keep existing resources
    if (!update) {
        error = "BeginUpdateResource failed: " + Utf8FromPath(exePath);
        return false;
    }

    for (const auto& entry : entries) {
        const std::string& name = entry.first;
        const std::vector<uint8_t>& data = *entry.second;
        if (data.empty()) {
            EndUpdateResource(update, TRUE);  // discard
            error = "empty data for resource " + name;
            return false;
        }
        if (!UpdateResourceA(update,
                             "BINARY",
                             name.c_str(),
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                             const_cast<void*>(static_cast<const void*>(data.data())),
                             static_cast<DWORD>(data.size()))) {
            EndUpdateResource(update, TRUE);
            error = "UpdateResource failed for " + name;
            return false;
        }
    }

    if (!EndUpdateResource(update, FALSE)) {
        error = "EndUpdateResource failed: " + Utf8FromPath(exePath);
        return false;
    }
    return true;
}

} // namespace

bool EmbedInstallerPeResources(const std::filesystem::path& installerTemplateExe,
                               const std::filesystem::path& resourceDir,
                               const std::filesystem::path& uninstallerTemplateExe,
                               std::string& error) {
    error.clear();

    // 1) Build the GUI resource zip once.
    std::vector<uint8_t> zipData;
    std::string zipError;
    if (!BuildResourceZip(resourceDir, zipData, zipError) || zipData.empty()) {
        error = zipError.empty() ? "Failed to build resources.zip" : zipError;
        return false;
    }

    // 2) Read the uninstaller template bytes AS-IS (不再在此注入 RES_ZIP)。
    //    方案B：RES_ZIP 在安装器里只存一份；磁盘上的 uninstall.exe 由安装器在安装收尾时
    //    通过 UpdateResource 把 RES_ZIP 注入进去（见 install_finalize），从而保持卸载器自包含，
    //    同时避免安装器里把同一份 RES_ZIP 嵌两份导致体积翻倍。
    std::vector<uint8_t> uninstallerBytes;
    if (!ReadAllBytes(uninstallerTemplateExe, uninstallerBytes, error) || uninstallerBytes.empty()) {
        error = error.empty() ? "read uninstaller template failed" : error;
        return false;
    }

    // 3) Inject RES_ZIP + UNINSTALLER_EXE into the installer template (single session).
    const std::vector<std::pair<std::string, const std::vector<uint8_t>*>> installerEntries = {
        {"RES_ZIP", &zipData},
        {"UNINSTALLER_EXE", &uninstallerBytes},
    };
    if (!UpdateBinaryResources(installerTemplateExe, installerEntries, error)) {
        return false;
    }
    return true;
}

} // namespace MultiThreadedInstaller
