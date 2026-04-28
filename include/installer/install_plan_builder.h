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
    UpgradeMigration,
    Repair,
};

struct InstallPathDecision {
    InstallTargetMode mode = InstallTargetMode::FreshInstall;
    std::string requestedInstallRoot;
    std::string resolvedInstallRoot;
    std::string diskCheckPath;
    std::string cleanupTargetInstallRoot;
    std::string shortcutCleanupTargetRoot;
};

struct ComponentSelectionPlan {
    bool hasComponents = false;
    std::vector<const ComponentConfig*> ordered;
    std::vector<std::string> embeddedFolders;
    std::vector<RegistryEntry> registryEntries;
    std::vector<std::string> killProcesses;
    bool autoStartup = false;
    bool desktopIcons = false;
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

bool BuildInstallExecutionPlan(const ExtendedInstallationMetadata& metadata,
                               InstallerPathResolver& pathResolver,
                               const InstallServiceOptions& options,
                               InstallExecutionPlan& plan,
                               std::string& error);

} // namespace MultiThreadedInstaller
