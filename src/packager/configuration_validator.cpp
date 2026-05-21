#include "packager/configuration_validator.h"

#include "common/utf8_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace MultiThreadedInstaller {
namespace {

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsLikelyRelativePath(const std::string& path) {
    if (path.empty()) {
        return true;
    }
    const fs::path fsPath = PathFromUtf8(path);
    if (fsPath.is_absolute()) {
        return false;
    }
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    return normalized.find(':') == std::string::npos;
}

bool ContainsParentTraversal(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    for (const auto& part : PathFromUtf8(path)) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

bool StartsWithInstallDirToken(const std::string& path) {
    const std::string lowered = ToLowerCopy(path);
    return lowered.rfind("%installdir%", 0) == 0 ||
           lowered.rfind("installdirectory", 0) == 0;
}

bool IsHttpsUrl(const std::string& value) {
    const std::string lowered = ToLowerCopy(value);
    return lowered.rfind("https://", 0) == 0;
}

bool IsSha256Hex(const std::string& value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isxdigit(ch) != 0;
           });
}

const char* CompressionAlgorithmName(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::LZMA2_XZ:
            return "xz";
        case CompressionAlgorithm::ZSTD:
            return "zstd";
        default:
            return "unknown";
    }
}

bool RegistryPathRequiresAdmin(const std::string& path) {
    const std::string lowered = ToLowerCopy(path);
    return lowered.rfind("hkey_local_machine", 0) == 0 ||
           lowered.rfind("hklm", 0) == 0;
}

bool InstallDirectoryRequiresAdmin(const std::string& path) {
    const std::string lowered = ToLowerCopy(path);
    return lowered.find("%programfiles%") != std::string::npos ||
           lowered.find("%programfiles(x86)%") != std::string::npos;
}

bool IsOneOf(std::string value, std::initializer_list<const char*> allowed) {
    value = ToLowerCopy(value);
    return std::any_of(allowed.begin(), allowed.end(), [&](const char* item) {
        return value == item;
    });
}

bool IsExplicitUserMachineOrBoth(UninstallEntryScope scope) {
    return scope == UninstallEntryScope::CURRENT_USER ||
           scope == UninstallEntryScope::LOCAL_MACHINE ||
           scope == UninstallEntryScope::BOTH;
}

void ValidateSystemUninstallLegacyEntries(const std::vector<SystemUninstallEntryCleanupItem>& entries,
                                          const std::string& fieldPath,
                                          ConfigurationValidator::ValidationResult& result) {
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const std::string position = fieldPath + "[" + std::to_string(i) + "]";
        if (entry.displayName.empty()) {
            result.errors.push_back("ERROR: " + position + ".displayName is required");
            result.isValid = false;
        }
        if (!IsExplicitUserMachineOrBoth(entry.scope)) {
            result.errors.push_back("ERROR: " + position + ".scope must be user, machine, or both");
            result.isValid = false;
        }
    }
}

} // namespace

ConfigurationValidator::ValidationResult ConfigurationValidator::validate(
    const PackagerConfiguration& config,
    const std::string& inputDirectory,
    const std::string& configDirectory) {
    ValidationResult result;

    if (config.schemaVersion != 3) {
        result.errors.push_back("ERROR: schemaVersion must be 3");
        result.isValid = false;
    }

    if (config.app.id.empty()) {
        result.errors.push_back("ERROR: Missing required field 'app.id'");
        result.isValid = false;
    }

    if (config.app.version.empty()) {
        result.errors.push_back("ERROR: Missing required field 'app.version'");
        result.isValid = false;
    }

    if (!validateApplicationName(config.app.name, result.errors)) {
        result.isValid = false;
    }

    const int compressionLevel = config.package.compression.level;
    if (compressionLevel != -1) {
        bool levelValid = false;
        if (config.package.compression.algorithm == CompressionAlgorithm::LZMA2_XZ) {
            levelValid = compressionLevel >= 0 && compressionLevel <= 9;
        } else if (config.package.compression.algorithm == CompressionAlgorithm::ZSTD) {
            levelValid = compressionLevel >= 1 && compressionLevel <= 22;
        }
        if (!levelValid) {
            result.errors.push_back(
                "ERROR: Invalid package.compression.level for algorithm '" +
                std::string(CompressionAlgorithmName(config.package.compression.algorithm)) + "'");
            result.isValid = false;
        }
    }

    if (config.installer.defaultDir.empty()) {
        result.errors.push_back("ERROR: Missing required field 'installer.defaultDir'");
        result.isValid = false;
    } else if (!validateTargetDirectory(config.installer.defaultDir, result.errors)) {
        result.isValid = false;
    }
    if (config.installer.directoryName.empty()) {
        result.errors.push_back("ERROR: Missing required field 'installer.directoryName'");
        result.isValid = false;
    }

    if (!config.installer.requireAdmin) {
        if (InstallDirectoryRequiresAdmin(config.installer.defaultDir)) {
            result.errors.push_back(
                "ERROR: installer.requireAdmin=false cannot use Program Files installer.defaultDir");
            result.isValid = false;
        }
        for (const auto& store : config.installer.installState.registries) {
            if (RegistryPathRequiresAdmin(store.path)) {
                result.errors.push_back(
                    "ERROR: installer.requireAdmin=false cannot write installer.installState.registries[] to HKLM");
                result.isValid = false;
                break;
            }
        }
        for (const auto& group : config.installer.registry.write) {
            if (RegistryPathRequiresAdmin(group.path)) {
                result.errors.push_back(
                    "ERROR: installer.requireAdmin=false cannot write installer.registry.write[] to HKLM");
                result.isValid = false;
                break;
            }
        }
        if ((config.installer.systemUninstallEntry.scope == UninstallEntryScope::LOCAL_MACHINE ||
             config.installer.systemUninstallEntry.scope == UninstallEntryScope::BOTH)) {
            result.errors.push_back(
                "ERROR: installer.requireAdmin=false cannot create machine system uninstall entry");
            result.isValid = false;
        }
    }

    if (config.installer.systemUninstallEntry.displayName.empty()) {
        result.errors.push_back("ERROR: Missing required field 'installer.systemUninstallEntry.displayName'");
        result.isValid = false;
    }
    ValidateSystemUninstallLegacyEntries(
        config.installer.cleanup.systemUninstallEntry.legacyEntries,
        "installer.cleanup.systemUninstallEntry.legacyEntries",
        result);

    const std::string icon = config.app.icon.empty() ? config.app.product.iconPath : config.app.icon;
    if (!icon.empty()) {
        fs::path iconPath = PathFromUtf8(icon);
        if (!iconPath.is_absolute()) {
            iconPath = PathFromUtf8(configDirectory) / iconPath;
        }
        if (!fs::exists(iconPath)) {
            result.errors.push_back("ERROR: Icon file not found: " + Utf8FromPath(iconPath));
            result.isValid = false;
        } else if (ToLowerCopy(Utf8FromPath(iconPath.extension())) != ".ico") {
            result.errors.push_back("ERROR: Icon file must be .ico: " + Utf8FromPath(iconPath));
            result.isValid = false;
        }
    }

    if (config.installer.mutex.empty()) {
        result.errors.push_back("ERROR: installer.mutex is required");
        result.isValid = false;
    }

    std::unordered_set<std::string> registryStoreIds;
    for (size_t i = 0; i < config.installer.installState.registries.size(); ++i) {
        const auto& store = config.installer.installState.registries[i];
        const std::string position = "installer.installState.registries[" + std::to_string(i) + "]";
        if (store.id.empty()) {
            result.errors.push_back("ERROR: " + position + ".id is required");
            result.isValid = false;
        } else if (!registryStoreIds.insert(store.id).second) {
            result.errors.push_back("ERROR: Duplicate installer.installState.registries id: " + store.id);
            result.isValid = false;
        }
        if (store.path.empty()) {
            result.errors.push_back("ERROR: " + position + ".path is required");
            result.isValid = false;
        }
        if (store.values.empty()) {
            result.errors.push_back("ERROR: " + position + ".values is required");
            result.isValid = false;
        }
        for (const auto& pair : store.values) {
            if (pair.second.key.empty()) {
                result.errors.push_back("ERROR: " + position + ".values." + pair.first + ".key is required");
                result.isValid = false;
            }
        }
    }

    std::unordered_set<std::string> fileStoreIds;
    for (size_t i = 0; i < config.installer.installState.files.size(); ++i) {
        const auto& store = config.installer.installState.files[i];
        const std::string position = "installer.installState.files[" + std::to_string(i) + "]";
        if (store.id.empty()) {
            result.errors.push_back("ERROR: " + position + ".id is required");
            result.isValid = false;
        } else if (!fileStoreIds.insert(store.id).second) {
            result.errors.push_back("ERROR: Duplicate installer.installState.files id: " + store.id);
            result.isValid = false;
        }
        if (store.path.empty()) {
            result.errors.push_back("ERROR: " + position + ".path is required");
            result.isValid = false;
        }
        if (ToLowerCopy(store.format) != "json") {
            result.errors.push_back("ERROR: " + position + ".format only supports json");
            result.isValid = false;
        }
        if (store.values.empty()) {
            result.errors.push_back("ERROR: " + position + ".values is required");
            result.isValid = false;
        }
    }

    const auto& detect = config.installer.installState.detect;
    const bool hasPrimaryDetect = !detect.primary.registry.empty() && !detect.primary.value.empty();
    if (detect.primary.registry.empty() != detect.primary.value.empty()) {
        result.errors.push_back(
            "ERROR: installer.installState.detect.primary.registry and value must be specified together");
        result.isValid = false;
    }
    if (hasPrimaryDetect) {
        auto registryIt = std::find_if(config.installer.installState.registries.begin(),
                                       config.installer.installState.registries.end(),
                                       [&](const InstallStateRegistryStoreConfig& store) {
                                           return store.id == detect.primary.registry;
                                       });
        if (registryIt == config.installer.installState.registries.end()) {
            result.errors.push_back("ERROR: installer.installState.detect.primary.registry references unknown registry store: " +
                                    detect.primary.registry);
            result.isValid = false;
        } else if (registryIt->values.find(detect.primary.value) == registryIt->values.end()) {
            result.errors.push_back("ERROR: installer.installState.detect.primary.value references unknown registry value: " +
                                    detect.primary.value);
            result.isValid = false;
        }
    }
    std::unordered_set<std::string> legacyDetectIds;
    bool hasLegacyDetect = false;
    for (size_t i = 0; i < detect.legacy.size(); ++i) {
        const auto& legacy = detect.legacy[i];
        const std::string position =
            "installer.installState.detect.legacy[" + std::to_string(i) + "]";
        if (legacy.id.empty()) {
            result.errors.push_back("ERROR: " + position + ".id is required");
            result.isValid = false;
        } else if (!legacyDetectIds.insert(legacy.id).second) {
            result.errors.push_back("ERROR: Duplicate installer.installState.detect.legacy id: " + legacy.id);
            result.isValid = false;
        }
        if (legacy.path.empty()) {
            result.errors.push_back("ERROR: " + position + ".path is required");
            result.isValid = false;
        }
        if (legacy.installDirValue.empty()) {
            result.errors.push_back("ERROR: " + position + ".installDirValue is required");
            result.isValid = false;
        }
        if (!legacy.path.empty() && !legacy.installDirValue.empty()) {
            hasLegacyDetect = true;
        }
    }
    if (!hasPrimaryDetect && !hasLegacyDetect) {
        result.errors.push_back(
            "ERROR: installer.installState.detect requires primary or at least one legacy entry");
        result.isValid = false;
    }

    std::unordered_set<std::string> payloadIds;
    payloadIds.reserve(config.installer.payload.size());
    if (config.installer.payload.empty()) {
        result.errors.push_back("ERROR: installer.payload must contain at least one entry");
        result.isValid = false;
    }
    for (size_t i = 0; i < config.installer.payload.size(); ++i) {
        const auto& payload = config.installer.payload[i];
        const std::string position = "installer.payload[" + std::to_string(i) + "]";
        if (payload.id.empty()) {
            result.errors.push_back("ERROR: " + position + ".id is required");
            result.isValid = false;
        } else if (!payloadIds.insert(payload.id).second) {
            result.errors.push_back("ERROR: Duplicate installer.payload id: " + payload.id);
            result.isValid = false;
        }
        if (!validateFolderExists(payload.source, inputDirectory, result.errors)) {
            result.isValid = false;
        }
        if (payload.target.empty()) {
            result.errors.push_back("ERROR: " + position + ".target is required");
            result.isValid = false;
        } else if (!validateTargetDirectory(payload.target, result.errors)) {
            result.isValid = false;
        }
    }

    if (!validateComponents(config, result.errors)) {
        result.isValid = false;
    }

    if (!IsOneOf(config.uninstaller.cleanup.missingManifestFallback,
                 {"safedirectoryfallback", "fail", "none", "disabled"})) {
        result.errors.push_back(
            "ERROR: Invalid uninstaller.cleanup.missingManifestFallback");
        result.isValid = false;
    }
    if (!IsOneOf(config.uninstaller.cleanup.installState,
                 {"delete", "markuninstalled", "keep"})) {
        result.errors.push_back("ERROR: Invalid uninstaller.cleanup.installState");
        result.isValid = false;
    }
    if (config.uninstaller.cleanup.systemUninstallEntry.displayName.empty()) {
        result.errors.push_back("ERROR: Missing required field 'uninstaller.cleanup.systemUninstallEntry.displayName'");
        result.isValid = false;
    }
    if (config.uninstaller.cleanup.systemUninstallEntry.scope == UninstallEntryScope::ANY) {
        result.errors.push_back("ERROR: Missing required field 'uninstaller.cleanup.systemUninstallEntry.scope'");
        result.isValid = false;
    }
    ValidateSystemUninstallLegacyEntries(
        config.uninstaller.cleanup.systemUninstallEntry.legacyEntries,
        "uninstaller.cleanup.systemUninstallEntry.legacyEntries",
        result);

    return result;
}

bool ConfigurationValidator::validateApplicationName(const std::string& name,
                                                     std::vector<std::string>& errors) {
    if (name.empty()) {
        errors.push_back("ERROR: Missing required field 'app.name'");
        return false;
    }

    const std::string illegalChars = "<>:\"/\\|?*";
    for (char c : name) {
        if (illegalChars.find(c) != std::string::npos) {
            errors.push_back("ERROR: Invalid app.name: illegal character '" + std::string(1, c) + "'");
            return false;
        }
        if (std::iscntrl(static_cast<unsigned char>(c))) {
            errors.push_back("ERROR: Invalid app.name: contains control character");
            return false;
        }
    }
    return true;
}

bool ConfigurationValidator::validateFolderExists(const std::string& folder,
                                                  const std::string& inputDir,
                                                  std::vector<std::string>& errors) {
    if (folder.empty()) {
        errors.push_back("ERROR: installer.payload[].source must not be empty");
        return false;
    }

    const fs::path folderPath = PathFromUtf8(inputDir) / PathFromUtf8(folder);
    if (!fs::exists(folderPath)) {
        errors.push_back("ERROR: Payload source folder does not exist: " + Utf8FromPath(folderPath));
        return false;
    }
    if (!fs::is_directory(folderPath)) {
        errors.push_back("ERROR: Payload source path is not a directory: " + Utf8FromPath(folderPath));
        return false;
    }
    return true;
}

bool ConfigurationValidator::validateTargetDirectory(const std::string& targetDir,
                                                     std::vector<std::string>& errors) {
    if (targetDir.empty()) {
        errors.push_back("ERROR: Target directory cannot be empty");
        return false;
    }

    const std::vector<std::string> validEnvVars = {
        "%InstallDir%",
        "%AppName%",
        "%Version%",
        "%InstallState%",
        "%ProgramFiles%",
        "%ProgramFiles(x86)%",
        "%AppData%",
        "%LocalAppData%",
        "%ProgramData%",
        "%USERPROFILE%"
    };

    bool hasValidEnvVar = false;
    for (const auto& envVar : validEnvVars) {
        if (targetDir.find(envVar) != std::string::npos) {
            hasValidEnvVar = true;
            break;
        }
    }

    if (targetDir.find('%') != std::string::npos && !hasValidEnvVar) {
        errors.push_back("ERROR: Invalid environment variable in target directory: " + targetDir);
        return false;
    }

    std::string pathToCheck = targetDir;
    for (const auto& envVar : validEnvVars) {
        size_t pos = pathToCheck.find(envVar);
        if (pos != std::string::npos) {
            pathToCheck.replace(pos, envVar.length(), "");
        }
    }

    const std::string illegalChars = "<>:\"|?*";
    for (char c : pathToCheck) {
        if (illegalChars.find(c) != std::string::npos) {
            errors.push_back("ERROR: Invalid target directory path: " + targetDir);
            return false;
        }
    }

    return true;
}

bool ConfigurationValidator::validateComponents(const PackagerConfiguration& config,
                                                std::vector<std::string>& errors) {
    if (config.installer.components.empty()) {
        return true;
    }

    bool valid = true;
    std::unordered_map<std::string, size_t> idIndex;
    std::unordered_set<std::string> payloadIds;
    for (const auto& payload : config.installer.payload) {
        payloadIds.insert(payload.id);
    }

    idIndex.reserve(config.installer.components.size());
    for (size_t i = 0; i < config.installer.components.size(); ++i) {
        const auto& component = config.installer.components[i];
        const std::string position = "installer.components[" + std::to_string(i) + "]";

        if (component.id.empty()) {
            errors.push_back("ERROR: " + position + ".id is required");
            valid = false;
            continue;
        }
        if (idIndex.find(component.id) != idIndex.end()) {
            errors.push_back("ERROR: Duplicate component id: " + component.id);
            valid = false;
        } else {
            idIndex.emplace(component.id, i);
        }

        if (component.required && !component.defaultSelected) {
            errors.push_back("ERROR: " + position + " is required but defaultSelected=false");
            valid = false;
        }

        for (const auto& folderId : component.folders) {
            if (payloadIds.find(folderId) == payloadIds.end()) {
                errors.push_back("ERROR: " + position + ".payload references unknown payload id: " + folderId);
                valid = false;
            }
        }

        if (component.source.type == ComponentSourceType::LOCAL) {
            if (component.source.local.installer.empty()) {
                errors.push_back("ERROR: " + position +
                                 ".install.command is required for local component installers");
                valid = false;
            }
            if (component.source.local.base.empty() ||
                !StartsWithInstallDirToken(component.source.local.base)) {
                errors.push_back("ERROR: " + position +
                                 ".source.local.base must start with %InstallDir%/installDirectory");
                valid = false;
            }
            if (!IsLikelyRelativePath(component.source.local.installer)) {
                errors.push_back("ERROR: " + position +
                                 ".source.local.installer must be a relative path under install dir");
                valid = false;
            }
            if (ContainsParentTraversal(component.source.local.installer) ||
                ContainsParentTraversal(component.source.local.base)) {
                errors.push_back("ERROR: " + position + ".source.local contains parent path traversal ('..')");
                valid = false;
            }
        } else if (component.source.type == ComponentSourceType::DOWNLOAD) {
            if (!IsHttpsUrl(component.source.download.url)) {
                errors.push_back("ERROR: " + position +
                                 ".install.url must start with https://");
                valid = false;
            }
            if (!component.source.download.sha256.empty() &&
                !IsSha256Hex(component.source.download.sha256)) {
                errors.push_back("ERROR: " + position +
                                 ".install.sha256 must be a 64-character hex SHA256");
                valid = false;
            }
            if (component.source.download.saveAs.empty() ||
                !StartsWithInstallDirToken(component.source.download.saveAs)) {
                errors.push_back("ERROR: " + position +
                                 ".install.saveAs must start with %InstallDir%/installDirectory");
                valid = false;
            }
            if (ContainsParentTraversal(component.source.download.saveAs)) {
                errors.push_back("ERROR: " + position + ".install.saveAs contains parent path traversal ('..')");
                valid = false;
            }
        }

    }

    for (size_t i = 0; i < config.installer.components.size(); ++i) {
        const auto& component = config.installer.components[i];
        const std::string position = "installer.components[" + std::to_string(i) + "]";
        for (const auto& dep : component.dependsOn) {
            if (idIndex.find(dep) == idIndex.end()) {
                errors.push_back("ERROR: " + position + ".dependsOn references unknown component: " + dep);
                valid = false;
            }
        }
    }

    enum class VisitState { Unvisited, Visiting, Visited };
    std::unordered_map<std::string, VisitState> states;
    states.reserve(idIndex.size());
    for (const auto& pair : idIndex) {
        states.emplace(pair.first, VisitState::Unvisited);
    }

    std::function<bool(const std::string&)> dfs = [&](const std::string& id) -> bool {
        auto itState = states.find(id);
        if (itState == states.end()) {
            return true;
        }
        if (itState->second == VisitState::Visiting) {
            errors.push_back("ERROR: Component dependency cycle detected at: " + id);
            return false;
        }
        if (itState->second == VisitState::Visited) {
            return true;
        }

        itState->second = VisitState::Visiting;
        const auto itIndex = idIndex.find(id);
        if (itIndex != idIndex.end()) {
            const auto& dependsOn = config.installer.components[itIndex->second].dependsOn;
            for (const auto& dep : dependsOn) {
                if (idIndex.find(dep) != idIndex.end() && !dfs(dep)) {
                    return false;
                }
            }
        }
        itState->second = VisitState::Visited;
        return true;
    };

    for (const auto& pair : idIndex) {
        if (!dfs(pair.first)) {
            valid = false;
            break;
        }
    }

    return valid;
}

} // namespace MultiThreadedInstaller
