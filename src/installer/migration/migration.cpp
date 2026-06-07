#include "installer/migration/migration.h"

#include "common/installer_logger.h"
#include "common/version_utils.h"
#include "installer/state/registry_utils.h"
#include "installer/platform/shortcut_startup_utils.h"

#include <filesystem>
#include <system_error>

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

void cleanupLegacyUninstallEntries(MigrationContext& ctx,
                                   const std::vector<std::string>& displayNames) {
    (void)ctx;
    for (const auto& name : displayNames) {
        if (name.empty()) {
            continue;
        }
        if (deleteSystemUninstallEntryByDisplayName(name, UninstallEntryScope::ANY)) {
            logInstallerInfo("[Migration] Removed legacy uninstall entry: " + name);
        }
    }
}

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

// ── 首批迁移：承接现 packager.yaml 的 SampleDesktopAppLegacy/1/2 名单 ───────
//
// 程序改名但路径未变 → 仅收尾旧名字残留（需求 §6.3 典型场景），不动任何当前路径。
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
