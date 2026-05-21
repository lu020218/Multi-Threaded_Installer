#pragma once

#include "common/archive_types.h"
#include "installer/install_service.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class InstallerPathResolver;

enum class InstallTargetMode {
    FreshInstall,
    OverwriteInstall,
    UpgradeInstall,
};

struct InstallPathDecision {
    InstallTargetMode mode = InstallTargetMode::FreshInstall;
    std::string requestedInstallRoot;
    std::string resolvedInstallRoot;
    std::string diskCheckPath;
    std::string cleanupTargetInstallRoot;
};

struct ComponentSelectionPlan {
    bool hasComponents = false;
    std::vector<const ComponentConfig*> ordered;
    std::vector<std::string> embeddedFolders;
};

struct InstallExecutionPlan {
    std::string effectiveAppId;
    std::string effectiveDirectoryName;
    bool hasPreviousInstall = false;
    std::string previousManifest;
    std::string previousInstallDir;
    InstallPathDecision pathDecision;
    std::vector<std::string> legacyDesktopShortcutCandidates;
    ComponentSelectionPlan componentPlan;
    std::vector<RegistryEntry> effectiveRegistry;
    std::vector<std::string> effectiveKillProcesses;
    bool effectiveAutoStartup = false;
    bool effectiveDesktopIcons = false;
    std::vector<std::string> selectedEmbeddedFolders;
    uint64_t totalInstallBytes = 0;
};

const char* InstallTargetModeName(InstallTargetMode mode);
std::string ResolveLanguageCode(const std::string& preferredLanguage);
std::string ResolveDesktopShortcutDisplayName(const ExtendedInstallationMetadata& metadata,
                                              const std::string& languageCode);
bool ResolveUpgradeInstallFromInstallStateDetect(const ExtendedInstallationMetadata& metadata,
                                                 InstallerPathResolver& pathResolver,
                                                 std::string& installDir,
                                                 std::string& manifestPath,
                                                 std::string& error);

bool BuildInstallExecutionPlan(const ExtendedInstallationMetadata& metadata,
                               InstallerPathResolver& pathResolver,
                               const InstallServiceOptions& options,
                               InstallExecutionPlan& plan,
                               std::string& error);

} // namespace MultiThreadedInstaller
