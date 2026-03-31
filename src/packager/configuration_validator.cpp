#include "packager/configuration_validator.h"

#include "common/utf8_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
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

bool IsHexSha256(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
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

bool IsSupportedDestinationType(const std::string& type) {
    static const std::unordered_set<std::string> kTypes = {
        "install",
        "programFiles",
        "programFilesX86",
        "appDataRoaming",
        "appDataLocal",
        "programData",
        "userProfile",
        "custom"
    };
    return kTypes.find(type) != kTypes.end();
}

} // namespace

ConfigurationValidator::ValidationResult ConfigurationValidator::validate(
    const PackagerConfiguration& config,
    const std::string& inputDirectory) {
    ValidationResult result;

    if (config.schemaVersion != 2) {
        result.errors.push_back("ERROR: schemaVersion must be 2");
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

    if (config.install.defaultDir.empty()) {
        result.errors.push_back("ERROR: Missing required field 'install.defaultDir'");
        result.isValid = false;
    } else if (!validateTargetDirectory(config.install.defaultDir, result.errors)) {
        result.isValid = false;
    }

    if (!config.app.product.iconPath.empty()) {
        fs::path iconPath = PathFromUtf8(config.app.product.iconPath);
        if (!iconPath.is_absolute()) {
            iconPath = PathFromUtf8(inputDirectory) / iconPath;
        }
        if (!fs::exists(iconPath)) {
            result.errors.push_back("ERROR: Icon file not found: " + Utf8FromPath(iconPath));
            result.isValid = false;
        } else if (ToLowerCopy(Utf8FromPath(iconPath.extension())) != ".ico") {
            result.errors.push_back("ERROR: Icon file must be .ico: " + Utf8FromPath(iconPath));
            result.isValid = false;
        }
    }

    for (const auto& reg : config.lifecycle.registry.onInstall) {
        if (reg.path.empty() || reg.key.empty()) {
            result.errors.push_back("ERROR: Invalid lifecycle.registry.onInstall entry: path and key are required");
            result.isValid = false;
            break;
        }
    }

    if ((config.install.installState.mode == InstallStateMode::REGISTRY ||
         config.install.installState.mode == InstallStateMode::BOTH) &&
        config.install.installState.registryPath.empty()) {
        result.errors.push_back("ERROR: install.installState.registryPath is required for registry mode");
        result.isValid = false;
    }

    if ((config.install.installState.mode == InstallStateMode::FILE ||
         config.install.installState.mode == InstallStateMode::BOTH) &&
        config.install.installState.filePath.empty()) {
        result.errors.push_back("ERROR: install.installState.filePath is required for file mode");
        result.isValid = false;
    }

    if (config.install.installState.useMutex && config.install.installState.mutexName.empty()) {
        result.errors.push_back("ERROR: install.installState.mutexName is required when useMutex is true");
        result.isValid = false;
    }

    std::unordered_set<std::string> folderIds;
    folderIds.reserve(config.layout.folders.size());
    for (const auto& folder : config.layout.folders) {
        if (folder.id.empty()) {
            result.errors.push_back("ERROR: layout.folders[].id is required");
            result.isValid = false;
            continue;
        }
        if (!folderIds.insert(folder.id).second) {
            result.errors.push_back("ERROR: Duplicate layout.folders id: " + folder.id);
            result.isValid = false;
        }
        if (!validateFolderExists(folder.source, inputDirectory, result.errors)) {
            result.isValid = false;
        }
        if (!IsSupportedDestinationType(folder.destination.type)) {
            result.errors.push_back("ERROR: Invalid field 'layout.folders[].destination.type': " +
                                    folder.destination.type);
            result.isValid = false;
        }
        if (folder.destination.type == "custom") {
            if (folder.destination.path.empty()) {
                result.errors.push_back(
                    "ERROR: layout.folders[].destination.path is required when destination.type is 'custom'");
                result.isValid = false;
            } else if (!validateTargetDirectory(folder.destination.path, result.errors)) {
                result.isValid = false;
            }
        }
    }

    if (!validateComponents(config, inputDirectory, result.errors)) {
        result.isValid = false;
    }

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
        errors.push_back("ERROR: layout.folders[].source must not be empty");
        return false;
    }

    const fs::path folderPath = PathFromUtf8(inputDir) / PathFromUtf8(folder);
    if (!fs::exists(folderPath)) {
        errors.push_back("ERROR: Layout source folder does not exist: " + Utf8FromPath(folderPath));
        return false;
    }
    if (!fs::is_directory(folderPath)) {
        errors.push_back("ERROR: Layout source path is not a directory: " + Utf8FromPath(folderPath));
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

    if (targetDir == "installDirectory") {
        return true;
    }

    const std::vector<std::string> validEnvVars = {
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
                                                const std::string& inputDirectory,
                                                std::vector<std::string>& errors) {
    if (config.layout.components.empty()) {
        return true;
    }

    bool valid = true;
    std::unordered_map<std::string, size_t> idIndex;
    std::unordered_set<std::string> folderIds;
    for (const auto& folder : config.layout.folders) {
        folderIds.insert(folder.id);
    }

    idIndex.reserve(config.layout.components.size());
    for (size_t i = 0; i < config.layout.components.size(); ++i) {
        const auto& component = config.layout.components[i];
        const std::string position = "layout.components[" + std::to_string(i) + "]";

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
            if (folderIds.find(folderId) == folderIds.end()) {
                errors.push_back("ERROR: " + position + ".folders references unknown folder id: " + folderId);
                valid = false;
            }
        }

        if (component.source.type == ComponentSourceType::LOCAL) {
            const std::string baseLower = ToLowerCopy(component.source.local.base);
            if (component.source.local.base.empty() ||
                (baseLower.find("%installdir%") != 0 && baseLower.find("installdirectory") != 0)) {
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
        }

        if (component.source.type == ComponentSourceType::DOWNLOAD) {
            const std::string urlLower = ToLowerCopy(component.source.download.url);
            if (urlLower.rfind("https://", 0) != 0) {
                errors.push_back("ERROR: " + position + ".source.download.url must use https://");
                valid = false;
            }
            if (!IsHexSha256(component.source.download.sha256)) {
                errors.push_back("ERROR: " + position +
                                 ".source.download.sha256 must be 64 hex characters");
                valid = false;
            }
        }
    }

    for (size_t i = 0; i < config.layout.components.size(); ++i) {
        const auto& component = config.layout.components[i];
        const std::string position = "layout.components[" + std::to_string(i) + "]";
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
            const auto& dependsOn = config.layout.components[itIndex->second].dependsOn;
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
