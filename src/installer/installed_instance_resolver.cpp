#include "installer/uninstall_manager.h"

#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"
#include "common/utf8_utils.h"

#include <cctype>
#include <filesystem>

namespace MultiThreadedInstaller {

namespace {

bool ExistingInstallDirectoryLooksValid(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(PathFromUtf8(path), ec) &&
           std::filesystem::is_directory(PathFromUtf8(path), ec);
}

std::string ExtractExecutablePathFromCommandLocal(const std::string& command) {
    std::string trimmed = command;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(),
                                                [](unsigned char c) { return !std::isspace(c); }));
    if (trimmed.empty()) {
        return {};
    }
    if (trimmed.front() == '"') {
        size_t endQuote = trimmed.find('"', 1);
        if (endQuote != std::string::npos) {
            return trimmed.substr(1, endQuote - 1);
        }
    }
    size_t firstSpace = trimmed.find_first_of(" \t");
    return firstSpace == std::string::npos ? trimmed : trimmed.substr(0, firstSpace);
}

}  // namespace

bool resolveExistingInstallInfo(const std::vector<std::string>& identityCandidates,
                                InstallerPathResolver& resolver,
                                std::string& manifestPath,
                                std::string& installDir,
                                std::string* matchedIdentity) {
    (void)resolver;
    manifestPath.clear();
    installDir.clear();
    if (matchedIdentity) {
        matchedIdentity->clear();
    }
    if (identityCandidates.empty()) {
        return false;
    }

    for (const auto& identity : identityCandidates) {
        if (identity.empty()) {
            continue;
        }

        std::string candidateManifest;
        std::string candidateInstallDir;
        std::string keyName = sanitizeRegistryKeyName(identity);

        const std::string hkcuPath =
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;
        const std::string hklmPath =
            "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;

        std::string legacyPath = "HKEY_CURRENT_USER\\Software\\" + identity;
        std::string legacyInstallDir;
        if (!readRegistryStringValue(legacyPath, "InstallDir", legacyInstallDir)) {
            std::string legacyPathHklm = "HKEY_LOCAL_MACHINE\\Software\\" + identity;
            readRegistryStringValue(legacyPathHklm, "InstallDir", legacyInstallDir);
        }
        if (!legacyInstallDir.empty()) {
            if (ExistingInstallDirectoryLooksValid(legacyInstallDir)) {
                candidateInstallDir = legacyInstallDir;
            }
            std::filesystem::path localManifest =
                PathFromUtf8(legacyInstallDir) / "install.manifest.json";
            if (std::filesystem::exists(localManifest)) {
                candidateManifest = Utf8FromPath(localManifest);
                if (candidateInstallDir.empty()) {
                    candidateInstallDir = legacyInstallDir;
                }
            }
        }

        if (candidateManifest.empty()) {
            std::string installLocation;
            if (!readRegistryStringValue(hkcuPath, "InstallLocation", installLocation)) {
                readRegistryStringValue(hklmPath, "InstallLocation", installLocation);
            }
            if (!installLocation.empty()) {
                if (ExistingInstallDirectoryLooksValid(installLocation)) {
                    candidateInstallDir = installLocation;
                }
                std::filesystem::path localManifest =
                    PathFromUtf8(installLocation) / "install.manifest.json";
                if (std::filesystem::exists(localManifest)) {
                    candidateManifest = Utf8FromPath(localManifest);
                    if (candidateInstallDir.empty()) {
                        candidateInstallDir = installLocation;
                    }
                }
            }
        }

        if (candidateManifest.empty()) {
            std::string uninstallString;
            if (!readRegistryStringValue(hkcuPath, "UninstallString", uninstallString)) {
                readRegistryStringValue(hklmPath, "UninstallString", uninstallString);
            }
            if (!uninstallString.empty()) {
                std::filesystem::path uninstallPath =
                    PathFromUtf8(ExtractExecutablePathFromCommandLocal(uninstallString));
                if (std::filesystem::exists(uninstallPath)) {
                    std::filesystem::path baseDir = uninstallPath.parent_path();
                    if (!baseDir.empty()) {
                        std::filesystem::path localManifest = baseDir / "install.manifest.json";
                        if (std::filesystem::exists(localManifest)) {
                            candidateManifest = Utf8FromPath(localManifest);
                            if (candidateInstallDir.empty()) {
                                candidateInstallDir = Utf8FromPath(baseDir);
                            }
                        }
                    }
                }
            }
        }

        if (!candidateManifest.empty()) {
            nlohmann::json manifest;
            if (readManifest(candidateManifest, manifest)) {
                std::string manifestInstallDir = manifest.value("installDir", "");
                if (!manifestInstallDir.empty() &&
                    ExistingInstallDirectoryLooksValid(manifestInstallDir)) {
                    candidateInstallDir = manifestInstallDir;
                }
            }
        }

        if (!candidateManifest.empty() || ExistingInstallDirectoryLooksValid(candidateInstallDir)) {
            manifestPath = std::move(candidateManifest);
            installDir = std::move(candidateInstallDir);
            if (matchedIdentity) {
                *matchedIdentity = identity;
            }
            return true;
        }
    }

    return false;
}

}  // namespace MultiThreadedInstaller
