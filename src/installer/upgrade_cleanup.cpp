#include "installer/upgrade_cleanup.h"

#include "common/utf8_utils.h"
#include "installer/installer_helpers.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <set>
#include <system_error>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

using json = nlohmann::json;

bool IsCancelled(const std::function<bool()>& cancellationCallback) {
    return cancellationCallback && cancellationCallback();
}

void EmitProgress(const UpgradeCleanupProgressCallback& progressCallback,
                  float progress,
                  const std::string& item) {
    if (!progressCallback) {
        return;
    }
    UpgradeCleanupProgressInfo info;
    info.progress = progress;
    info.currentItem = item;
    progressCallback(info);
}

bool ReadManifestJson(const std::string& manifestPath, json& outManifest) {
    if (manifestPath.empty()) {
        return false;
    }
    std::ifstream in(toLongPath(PathFromUtf8(manifestPath)), std::ios::binary);
    if (!in) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty()) {
        return false;
    }
    outManifest = json::parse(content, nullptr, false);
    return !outManifest.is_discarded();
}

std::string NormalizePath(const std::filesystem::path& path) {
    return normalizePathForCompare(Utf8FromPath(path.lexically_normal()));
}

bool IsPathUnderOrEqual(const std::filesystem::path& candidate,
                        const std::filesystem::path& root) {
    const std::string normalizedCandidate = NormalizePath(candidate);
    const std::string normalizedRoot = NormalizePath(root);
    if (normalizedCandidate.empty() || normalizedRoot.empty()) {
        return false;
    }
    if (normalizedCandidate == normalizedRoot) {
        return true;
    }
    if (normalizedCandidate.size() <= normalizedRoot.size()) {
        return false;
    }
    if (normalizedCandidate.compare(0, normalizedRoot.size(), normalizedRoot) != 0) {
        return false;
    }
    const char sep = normalizedCandidate[normalizedRoot.size()];
    return sep == '\\' || sep == '/';
}

std::vector<std::string> CollectManifestFiles(const json& manifest) {
    std::vector<std::string> files;
    if (!manifest.contains("files") || !manifest["files"].is_array()) {
        return files;
    }
    for (const auto& item : manifest["files"]) {
        if (item.is_string()) {
            files.push_back(item.get<std::string>());
        }
    }
    return files;
}

} // namespace

bool cleanupPreviousInstallForUpgrade(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const std::string& newInstallDir,
    ConsoleInterface& console,
    const UpgradeCleanupProgressCallback& progressCallback,
    const std::function<bool()>& cancellationCallback) {
    std::filesystem::path previousRoot = PathFromUtf8(previousInstallDir).lexically_normal();
    if (previousInstallDir.empty() || previousRoot.empty()) {
        console.showWarning("Upgrade cleanup skipped: previous install directory is empty.");
        return false;
    }

    std::string normalizedOld = normalizePathForCompare(previousInstallDir);
    std::string normalizedNew = normalizePathForCompare(newInstallDir);
    if (!normalizedOld.empty() && !normalizedNew.empty() && normalizedOld == normalizedNew) {
        console.showInfo("Upgrade cleanup skipped: previous and new install directories are the same.");
        return true;
    }

    std::error_code existsEc;
    if (!std::filesystem::exists(previousRoot, existsEc)) {
        console.showInfo("Upgrade cleanup skipped: previous install directory not found.");
        return true;
    }
    if (!std::filesystem::is_directory(previousRoot, existsEc)) {
        console.showWarning("Upgrade cleanup skipped: previous install path is not a directory.");
        return false;
    }

    EmitProgress(progressCallback, 0.0f, "Preparing upgrade cleanup");
    if (IsCancelled(cancellationCallback)) {
        console.showWarning("Upgrade cleanup cancelled before start.");
        return false;
    }

    json manifest;
    std::vector<std::string> manifestFiles;
    if (ReadManifestJson(manifestPath, manifest)) {
        manifestFiles = CollectManifestFiles(manifest);
    } else {
        console.showWarning("Upgrade cleanup: failed to read previous manifest, fallback to minimal cleanup.");
    }

    std::vector<std::filesystem::path> filesToDelete;
    filesToDelete.reserve(manifestFiles.size() + 2);
    std::set<std::string> seen;

    auto tryAddFile = [&](const std::filesystem::path& path, bool requiredUnderOldRoot) {
        if (path.empty()) {
            return;
        }
        std::filesystem::path normalizedPath = path.lexically_normal();
        if (requiredUnderOldRoot && !IsPathUnderOrEqual(normalizedPath, previousRoot)) {
            console.showWarning("Upgrade cleanup skipped out-of-root file: " + Utf8FromPath(normalizedPath));
            return;
        }
        const std::string key = NormalizePath(normalizedPath);
        if (key.empty()) {
            return;
        }
        if (seen.insert(key).second) {
            filesToDelete.push_back(std::move(normalizedPath));
        }
    };

    for (const auto& file : manifestFiles) {
        if (file.empty()) {
            continue;
        }
        std::filesystem::path path = PathFromUtf8(file);
        if (!path.is_absolute()) {
            path = previousRoot / path;
        }
        tryAddFile(path, true);
    }

    tryAddFile(previousRoot / "uninstall.exe", true);
    tryAddFile(previousRoot / "install.manifest.json", true);

    const size_t totalFiles = filesToDelete.size();
    for (size_t i = 0; i < totalFiles; ++i) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade cleanup cancelled while deleting files.");
            return false;
        }
        const std::filesystem::path& filePath = filesToDelete[i];
        std::error_code removeEc;
        std::filesystem::remove(toLongPath(filePath), removeEc);
        if (removeEc && std::filesystem::exists(filePath)) {
            console.showWarning("Upgrade cleanup failed to remove file: " + Utf8FromPath(filePath));
        }

        float progress = 0.8f;
        if (totalFiles > 0) {
            progress = 0.8f * static_cast<float>(i + 1) / static_cast<float>(totalFiles);
        }
        EmitProgress(progressCallback, progress, "Removing old file: " + Utf8FromPath(filePath));
    }

    if (IsCancelled(cancellationCallback)) {
        console.showWarning("Upgrade cleanup cancelled before directory cleanup.");
        return false;
    }

    std::vector<std::filesystem::path> directories;
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(toLongPath(previousRoot), options)) {
        if (entry.is_directory()) {
            directories.push_back(entry.path());
        }
    }
    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
        return a.native().size() > b.native().size();
    });

    for (const auto& dir : directories) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade cleanup cancelled while removing empty directories.");
            return false;
        }
        std::error_code ec;
        if (std::filesystem::is_empty(dir, ec)) {
            std::filesystem::remove(toLongPath(dir), ec);
        }
    }
    EmitProgress(progressCallback, 0.95f, "Removing empty directories from previous install root");

    std::error_code rootEc;
    if (std::filesystem::is_empty(previousRoot, rootEc)) {
        std::filesystem::remove(toLongPath(previousRoot), rootEc);
        if (!rootEc) {
            console.showInfo("Removed previous install root: " + Utf8FromPath(previousRoot));
        }
    } else {
        console.showInfo("Previous install root is not empty after upgrade cleanup: " +
                         Utf8FromPath(previousRoot));
    }

    EmitProgress(progressCallback, 1.0f, "Upgrade cleanup completed");
    return true;
}

} // namespace MultiThreadedInstaller
