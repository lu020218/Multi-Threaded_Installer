#include "installer/migration/migration.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "common/version_utils.h"
#include "installer/platform/path_resolver.h"
#include "installer/state/registry_utils.h"
#include "installer/platform/shortcut_startup_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include <json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace MultiThreadedInstaller {
namespace migration {
namespace {

// ── 迁移可复用的收尾原语 ───────────────────────────────────────────────────

// [旧版本-清理:系统卸载入口] 按旧产品名推导键精确删除「程序和功能」里的旧版本卸载入口
// （与本引擎写入规则同一推导，见 deleteSystemUninstallEntry）。
void cleanupLegacyUninstallEntries(MigrationContext& ctx,
                                   const std::vector<std::string>& displayNames) {
    (void)ctx;
    for (const auto& name : displayNames) {
        if (name.empty()) {
            continue;
        }
        if (deleteSystemUninstallEntry(name)) {
            logInstallerInfo("[Migration] Removed legacy uninstall entry: " + name);
        }
    }
}

// [旧版本-清理:注册表] 删除旧版本残留的注册表键（如改名前的 HKCU/HKLM\Software\<旧名>）。
void deleteLegacyRegistry(MigrationContext& ctx, const std::vector<std::string>& keyPaths) {
    (void)ctx;
    for (const auto& path : keyPaths) {
        if (path.empty()) {
            continue;
        }
        if (deleteRegistryPath(path)) {
            logInstallerInfo("[Migration] Removed legacy registry key: " + path);
        }
    }
}

// [旧版本-清理:快捷方式] 删除旧版本的桌面 + 开始菜单快捷方式。
void removeLegacyShortcuts(MigrationContext& ctx, const std::vector<std::string>& names) {
    (void)ctx;
    for (const auto& name : names) {
        if (name.empty()) {
            continue;
        }
        deleteDesktopShortcut(name);
        deleteStartMenuShortcut(name);
        logInstallerInfo("[Migration] Cleaned legacy shortcut: " + name);
    }
}

// [旧版本-清理:开机自启] 删除旧版本注册的开机自启动项。
void removeLegacyStartup(MigrationContext& ctx, const std::vector<std::string>& names) {
    (void)ctx;
    for (const auto& name : names) {
        if (name.empty()) {
            continue;
        }
        if (removeAutoStartup(name)) {
            logInstallerInfo("[Migration] Removed legacy startup entry: " + name);
        }
    }
}

// [旧版本-清理:残留路径] 删除旧版本在 %LocalAppData%/%AppData% 等处的残留数据目录。
void removeLegacyPaths(MigrationContext& ctx, const std::vector<std::string>& paths) {
    (void)ctx;
#ifdef _WIN32
    for (const auto& raw : paths) {
        if (raw.empty()) {
            continue;
        }
        // 展开 %LocalAppData% 等环境变量后删除。
        DWORD needed = ExpandEnvironmentStringsA(raw.c_str(), nullptr, 0);
        std::string expanded = raw;
        if (needed > 0) {
            std::string buffer(needed, '\0');
            if (ExpandEnvironmentStringsA(raw.c_str(), buffer.data(), needed) > 0) {
                if (!buffer.empty() && buffer.back() == '\0') {
                    buffer.pop_back();
                }
                expanded = buffer;
            }
        }
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::u8path(expanded), ec);
        if (!ec) {
            logInstallerInfo("[Migration] Removed legacy path: " + expanded);
        }
    }
#else
    (void)paths;
#endif
}

// ── 旧版本配置迁移原语（改名迁移：UniAssistant → weclaw-desktop）─────────────
#ifdef _WIN32

// 展开环境变量占位为真实路径：复用现有 InstallerPathResolver::expandEnvironmentVariables
// （UTF-8，内部走 ExpandEnvironmentStringsW，Unicode 安全）。
// 注意：SYSTEM / 换管理员账户提权时展开的是那个账户的 profile，非当前登录用户（已知局限）。
std::filesystem::path ExpandEnvToPath(const std::string& raw) {
    InstallerPathResolver resolver;
    return PathFromUtf8(resolver.expandEnvironmentVariables(raw));
}

// 宽松解析布尔：接受 JSON bool、数字(非 0=true)、字符串 "true/false/1/0/yes/no/on/off"（大小写不敏感）。
bool ParseLenientBool(const nlohmann::json& value, bool& out) {
    if (value.is_boolean()) {
        out = value.get<bool>();
        return true;
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        out = value.get<long long>() != 0;
        return true;
    }
    if (value.is_number_float()) {
        out = value.get<double>() != 0.0;
        return true;
    }
    if (value.is_string()) {
        std::string s = value.get<std::string>();
        const size_t b = s.find_first_not_of(" \t\r\n");
        const size_t e = s.find_last_not_of(" \t\r\n");
        s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (s == "true" || s == "1" || s == "yes" || s == "on") {
            out = true;
            return true;
        }
        if (s == "false" || s == "0" || s == "no" || s == "off") {
            out = false;
            return true;
        }
    }
    return false;
}

// 读整文件为字符串（失败返回 false）。
bool ReadTextFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// 原子写 JSON：写临时文件 + MoveFileEx 覆盖替换。失败返回 false（并记具体原因）。
bool WriteJsonAtomic(const std::filesystem::path& dst, const nlohmann::json& value) {
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        logInstallerWarning("[Migration] smartbar: create_directories failed: " + ec.message());
        return false;
    }
    std::filesystem::path tmp = dst;
    tmp += L".mti_tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            logInstallerWarning("[Migration] smartbar: open temp failed: " + Utf8FromPath(tmp));
            return false;
        }
        const std::string dump = value.dump(2);
        out.write(dump.data(), static_cast<std::streamsize>(dump.size()));
        out.close();
        if (!out) {
            logInstallerWarning("[Migration] smartbar: temp write failed: " + Utf8FromPath(tmp));
            std::error_code rmEc;
            std::filesystem::remove(tmp, rmEc);
            return false;
        }
    }
    // COPY_ALLOWED：当 %APPDATA% 被重定向/联接到别的卷时，MoveFileEx 会报 ERROR_NOT_SAME_DEVICE(17)，
    // 加此标志允许回退为「复制+删源」，保证替换可用（best-effort 迁移不苛求纯 rename 的原子性）。
    if (!MoveFileExW(tmp.c_str(), dst.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        const DWORD moveErr = GetLastError();
        logInstallerWarning("[Migration] smartbar: MoveFileEx failed (code=" +
                            std::to_string(moveErr) + ") tmp=" + Utf8FromPath(tmp));
        std::error_code rmEc;
        std::filesystem::remove(tmp, rmEc);
        return false;
    }
    return true;
}

// [配置迁移] 把旧版本 %LOCALAPPDATA%\UniAssistant\pedestal\config.json 的 showSmartBar
// 迁到新版本 %APPDATA%\weclaw-desktop\pedestal\userConfig.json 的 smartbar.enable。
// 版本门控由迁移表 atVersion="7.0.0" 负责（仅 fromVersion < 7.0.0 升级时执行）。
// best-effort：任何失败只记警告并返回 true，绝不因配置迁移把安装判失败。
bool migrateSmartBarConfig(MigrationContext& ctx) {
    (void)ctx;
    try {
        const std::filesystem::path src =
            ExpandEnvToPath("%LOCALAPPDATA%\\UniAssistant\\pedestal\\config.json");
        const std::filesystem::path dst =
            ExpandEnvToPath("%APPDATA%\\weclaw-desktop\\pedestal\\userConfig.json");

        // 1) 载入目标（存在则读）；一次性标记已存在 → 跳过（严格只迁一次）。
        nlohmann::json target = nlohmann::json::object();
        std::error_code ec;
        if (std::filesystem::exists(dst, ec)) {
            std::string content;
            if (ReadTextFile(dst, content) && !content.empty()) {
                target = nlohmann::json::parse(content, nullptr, false);
                if (target.is_discarded()) {
                    logInstallerWarning("[Migration] smartbar: target userConfig.json unparsable; skip.");
                    return true;
                }
            }
        }
        if (!target.is_object()) {
            target = nlohmann::json::object();
        }
        if (target.contains("_migrations") && target["_migrations"].is_object() &&
            target["_migrations"].value("uniAssistantSmartBar", false)) {
            logInstallerInfo("[Migration] smartbar: already migrated; skip.");
            return true;
        }

        // 2) 读源 showSmartBar（源缺失/无键/解析不出 → 跳过，不打标记）。
        if (!std::filesystem::exists(src, ec)) {
            logInstallerInfo("[Migration] smartbar: source config.json not found (" +
                             Utf8FromPath(src) + "); nothing to migrate.");
            return true;
        }
        std::string sourceContent;
        if (!ReadTextFile(src, sourceContent)) {
            logInstallerWarning("[Migration] smartbar: cannot open source config.json; skip.");
            return true;
        }
        nlohmann::json source = nlohmann::json::parse(sourceContent, nullptr, false);
        if (source.is_discarded() || !source.is_object() || !source.contains("showSmartBar")) {
            logInstallerInfo("[Migration] smartbar: source has no valid showSmartBar; skip.");
            return true;
        }
        bool enable = false;
        if (!ParseLenientBool(source["showSmartBar"], enable)) {
            logInstallerWarning("[Migration] smartbar: showSmartBar not a recognizable bool; skip.");
            return true;
        }

        // 3) 写目标：smartbar.enable + 一次性标记，保留其它键，原子写。旧 config.json 不动。
        if (!target["smartbar"].is_object()) {
            target["smartbar"] = nlohmann::json::object();
        }
        target["smartbar"]["enable"] = enable;
        if (!target["_migrations"].is_object()) {
            target["_migrations"] = nlohmann::json::object();
        }
        target["_migrations"]["uniAssistantSmartBar"] = true;

        if (!WriteJsonAtomic(dst, target)) {
            logInstallerWarning("[Migration] smartbar: failed to write target userConfig.json (" +
                                Utf8FromPath(dst) + "); skip.");
            return true;
        }
        logInstallerInfo(std::string("[Migration] smartbar: migrated showSmartBar -> smartbar.enable=") +
                         (enable ? "true" : "false") + " target=" + Utf8FromPath(dst));
        return true;
    } catch (const std::exception& e) {
        logInstallerWarning(std::string("[Migration] smartbar: unexpected error, skip: ") + e.what());
        return true;
    } catch (...) {
        logInstallerWarning("[Migration] smartbar: unexpected error, skip.");
        return true;
    }
}

#else  // !_WIN32
bool migrateSmartBarConfig(MigrationContext&) { return true; }
#endif

// ── 首批迁移：承接现 packager.yaml 的 SampleDesktopAppLegacy/1/2 名单 ───────
//
// 程序改名但路径未变 → 仅收尾旧名字残留（需求 §6.3 典型场景），不动任何当前路径。
// [旧版本-清理:汇总] 一个迁移节点把上述各类清理（系统卸载入口/注册表/快捷方式/开机自启/
// 残留路径）按本次版本的需要组合起来；新增旧版本收尾时新增迁移节点即可。
bool migrate_7_0_0(MigrationContext& ctx) {
    cleanupLegacyUninstallEntries(ctx, {"SampleDesktopAppLegacy",
                                        "SampleDesktopAppLegacy1",
                                        "SampleDesktopAppLegacy2"});
    deleteLegacyRegistry(ctx, {R"(HKCU\Software\SampleDesktopAppLegacy)",
                               R"(HKLM\Software\SampleDesktopAppLegacy)"});
    removeLegacyShortcuts(ctx, {"Sample Desktop App", "SampleDesktopApp Legacy"});
    removeLegacyStartup(ctx, {"SampleDesktopAppLegacy"});
    removeLegacyPaths(ctx, {R"(%LocalAppData%\SampleDesktopAppLegacy)",
                            R"(%AppData%\SampleDesktopAppLegacy)"});
    // 旧版本配置迁移：showSmartBar → smartbar.enable（改名 UniAssistant → weclaw-desktop）。
    // best-effort，仅 fromVersion < 7.0.0 升级时随本节点执行一次。
    migrateSmartBarConfig(ctx);
    return true;
}

} // namespace

const std::vector<Migration>& Registry() {
    // 按版本升序排列。新增节点往后追加；旧节点无用户后连同 apply 函数一并删除。
    static const std::vector<Migration> table = {
        {"7.0.0", &migrate_7_0_0},
    };
    return table;
}

bool RunPending(MigrationContext& ctx) {
    // 全新安装（无已装版本）没有旧状态可迁移，直接跳过。
    if (ctx.fromVersion.empty()) {
        logInstallerInfo("[Migration] Fresh install; no pending migrations.");
        return true;
    }

    for (const auto& migration : Registry()) {
        // 仅执行 atVersion > fromVersion 的迁移（升级跨过该节点时执行一次）。
        if (compareSemanticVersion(migration.atVersion, ctx.fromVersion) <= 0) {
            continue;
        }
        logInstallerInfo(std::string("[Migration] Applying migration at ") + migration.atVersion +
                         " (from " + ctx.fromVersion + ")");
        if (!migration.apply || !migration.apply(ctx)) {
            logInstallerError(std::string("[Migration] Migration failed at ") + migration.atVersion);
            return false;
        }
    }
    return true;
}

} // namespace migration
} // namespace MultiThreadedInstaller
