#include "packager/pe_resource_embedder.h"

#include "packager/resource_zip_builder.h"
#include "packager/pe_resource_session.h"
#include "packager/version_info_updater.h"
#include "packager/icon_updater.h"
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

// 只做 UpdateResource（写入已打开的会话句柄），不 Begin/End。Type/name 为大写字符串，
// 运行期用 FindResourceA(name, "BINARY") 匹配。供带重试的会话或单会话编排复用。
bool ApplyBinaryResourcesInto(
    void* update,
    const std::vector<std::pair<std::string, const std::vector<uint8_t>*>>& entries,
    std::string& error) {
    HANDLE hUpdate = static_cast<HANDLE>(update);
    for (const auto& entry : entries) {
        const std::string& name = entry.first;
        const std::vector<uint8_t>& data = *entry.second;
        if (data.empty()) {
            error = "empty data for resource " + name;
            return false;
        }
        if (!UpdateResourceA(hUpdate,
                             "BINARY",
                             name.c_str(),
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                             const_cast<void*>(static_cast<const void*>(data.data())),
                             static_cast<DWORD>(data.size()))) {
            error = "UpdateResource failed for " + name;
            return false;
        }
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

    // 3) Inject RES_ZIP + UNINSTALLER_EXE into the installer template (带重试的单会话).
    const std::vector<std::pair<std::string, const std::vector<uint8_t>*>> installerEntries = {
        {"RES_ZIP", &zipData},
        {"UNINSTALLER_EXE", &uninstallerBytes},
    };
    return RunResourceUpdateSession(
        installerTemplateExe.wstring(),
        [&installerEntries](void* update, std::string& err) {
            return ApplyBinaryResourcesInto(update, installerEntries, err);
        },
        error);
}

bool EmbedAllInstallerPeResources(const std::filesystem::path& installerTemplateExe,
                                  bool requireAdmin,
                                  const std::string& iconPath,
                                  const VersionInfoData& versionInfo,
                                  const std::filesystem::path& resourceDir,
                                  const std::filesystem::path& uninstallerTemplateExe,
                                  std::string& error,
                                  std::vector<std::string>& warnings) {
    error.clear();
    warnings.clear();

    // 数据准备（无文件锁风险）：GUI 皮肤 zip + 卸载器字节，会话前一次性构建好。
    std::vector<uint8_t> zipData;
    std::string zipError;
    if (!BuildResourceZip(resourceDir, zipData, zipError) || zipData.empty()) {
        error = zipError.empty() ? "Failed to build resources.zip" : zipError;
        return false;
    }
    std::vector<uint8_t> uninstallerBytes;
    if (!ReadAllBytes(uninstallerTemplateExe, uninstallerBytes, error) || uninstallerBytes.empty()) {
        error = error.empty() ? "read uninstaller template failed" : error;
        return false;
    }
    const std::vector<std::pair<std::string, const std::vector<uint8_t>*>> binaryEntries = {
        {"RES_ZIP", &zipData},
        {"UNINSTALLER_EXE", &uninstallerBytes},
    };

    // 关键：清单 + 图标 + 版本 + RES_ZIP + 卸载器 全部在「同一个」资源会话里写入，
    // 只重写一次 exe（原来是 4~5 次），大幅缩小杀软锁窗口；会话本身再带重试兜底。
    std::vector<std::string> localWarnings;
    const bool applied = RunResourceUpdateSession(
        installerTemplateExe.wstring(),
        [&](void* update, std::string& err) -> bool {
            localWarnings.clear();  // 会话若重试，只保留最后一轮的告警。
            // 清单执行级别（必需，失败即整体失败）。
            if (!ApplyInstallerManifestInto(update, requireAdmin, err)) {
                return false;
            }
            // 图标（可选，失败仅告警：坏图标不该阻断打包）。
            if (!iconPath.empty()) {
                std::string iconErr;
                if (!ApplyInstallerIconInto(update, iconPath, iconErr)) {
                    localWarnings.push_back("icon skipped: " + iconErr);
                }
            }
            // 版本资源（失败仅告警）。
            std::string verErr;
            if (!ApplyInstallerVersionInfoInto(update, versionInfo, verErr)) {
                localWarnings.push_back("version info skipped: " + verErr);
            }
            // RES_ZIP + 卸载器（必需，失败即整体失败）。
            if (!ApplyBinaryResourcesInto(update, binaryEntries, err)) {
                return false;
            }
            return true;
        },
        error);

    warnings = std::move(localWarnings);
    return applied;
}

} // namespace MultiThreadedInstaller
