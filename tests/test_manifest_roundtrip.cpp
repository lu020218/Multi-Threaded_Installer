#include "installer/uninstall_manager.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using MultiThreadedInstaller::InstallStateConfig;
using MultiThreadedInstaller::InstallStateMode;
using MultiThreadedInstaller::RegistryEntry;
using MultiThreadedInstaller::RegistryValueType;
using MultiThreadedInstaller::readManifest;
using MultiThreadedInstaller::writeManifest;
using json = nlohmann::json;

void AssertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<RegistryEntry> ParseRegistry(const json& manifest) {
    std::vector<RegistryEntry> registry;
    const auto& arr = manifest.at("registry");
    registry.reserve(arr.size());
    for (const auto& item : arr) {
        RegistryEntry entry;
        entry.path = item.at("path").get<std::string>();
        entry.key = item.at("key").get<std::string>();
        entry.value = item.at("value").get<std::string>();
        entry.type = static_cast<RegistryValueType>(item.at("type").get<int>());
        registry.push_back(entry);
    }
    return registry;
}

InstallStateConfig ParseInstallState(const json& manifest) {
    InstallStateConfig state;
    const auto& raw = manifest.at("installState");
    state.mode = static_cast<InstallStateMode>(raw.at("mode").get<int>());
    state.registryPath = raw.at("registryPath").get<std::string>();
    state.registryKey = raw.at("registryKey").get<std::string>();
    state.filePath = raw.at("filePath").get<std::string>();
    state.useMutex = raw.at("useMutex").get<bool>();
    state.mutexName = raw.at("mutexName").get<std::string>();
    return state;
}

void ValidateManifestFields(const json& manifest,
                            const std::string& appName,
                            const std::string& version,
                            const std::string& installDir,
                            const std::vector<std::string>& files,
                            const std::vector<RegistryEntry>& registry,
                            const std::vector<std::string>& killProcesses,
                            const InstallStateConfig& installState,
                            const std::string& uninstallPath,
                            const std::string& language) {
    AssertTrue(manifest.at("appName").get<std::string>() == appName, "appName mismatch");
    AssertTrue(manifest.at("configVersion").get<std::string>() == version, "configVersion mismatch");
    AssertTrue(manifest.at("installDir").get<std::string>() == installDir, "installDir mismatch");
    AssertTrue(manifest.at("uninstallPath").get<std::string>() == uninstallPath, "uninstallPath mismatch");
    AssertTrue(manifest.at("language").get<std::string>() == language, "language mismatch");

    const auto parsedFiles = manifest.at("files").get<std::vector<std::string>>();
    AssertTrue(parsedFiles == files, "files mismatch");

    const auto parsedKill = manifest.at("killProcesses").get<std::vector<std::string>>();
    AssertTrue(parsedKill == killProcesses, "killProcesses mismatch");

    const auto& parsedRegistry = manifest.at("registry");
    AssertTrue(parsedRegistry.size() == registry.size(), "registry size mismatch");
    for (size_t i = 0; i < registry.size(); ++i) {
        AssertTrue(parsedRegistry[i].at("path").get<std::string>() == registry[i].path,
                   "registry path mismatch");
        AssertTrue(parsedRegistry[i].at("key").get<std::string>() == registry[i].key,
                   "registry key mismatch");
        AssertTrue(parsedRegistry[i].at("value").get<std::string>() == registry[i].value,
                   "registry value mismatch");
        AssertTrue(parsedRegistry[i].at("type").get<int>() == static_cast<int>(registry[i].type),
                   "registry type mismatch");
    }

    const auto& parsedState = manifest.at("installState");
    AssertTrue(parsedState.at("mode").get<int>() == static_cast<int>(installState.mode), "installState.mode mismatch");
    AssertTrue(parsedState.at("registryPath").get<std::string>() == installState.registryPath,
               "installState.registryPath mismatch");
    AssertTrue(parsedState.at("registryKey").get<std::string>() == installState.registryKey,
               "installState.registryKey mismatch");
    AssertTrue(parsedState.at("filePath").get<std::string>() == installState.filePath,
               "installState.filePath mismatch");
    AssertTrue(parsedState.at("useMutex").get<bool>() == installState.useMutex,
               "installState.useMutex mismatch");
    AssertTrue(parsedState.at("mutexName").get<std::string>() == installState.mutexName,
               "installState.mutexName mismatch");
}

} // namespace

int main() {
    try {
        const auto tempRoot = std::filesystem::temp_directory_path() / "mti_manifest_roundtrip_test";
        const auto manifestDir = tempRoot / "manifests";
        const auto manifestPathA = manifestDir / "install_a.manifest.json";
        const auto manifestPathB = manifestDir / "install_b.manifest.json";

        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);

        const std::string appName = "RoundTripApp";
        const std::string version = "2.5.0";
        const std::string installDir = "C:\\Program Files\\RoundTripApp \\u6D4B\\u8BD5";
        const std::vector<std::string> files = {
            "C:\\Program Files\\RoundTripApp \\u6D4B\\u8BD5\\RoundTripApp.exe",
            "C:\\Program Files\\RoundTripApp \\u6D4B\\u8BD5\\config\\settings.json"
        };

        std::vector<RegistryEntry> registry;
        {
            RegistryEntry installEntry;
            installEntry.path = "HKEY_CURRENT_USER\\Software\\RoundTripApp";
            installEntry.key = "InstallDir";
            installEntry.value = "%InstallDir%";
            installEntry.type = RegistryValueType::EXPAND_STRING;
            registry.push_back(installEntry);

            RegistryEntry versionEntry;
            versionEntry.path = "HKEY_CURRENT_USER\\Software\\RoundTripApp";
            versionEntry.key = "Version";
            versionEntry.value = "%Version%";
            versionEntry.type = RegistryValueType::STRING;
            registry.push_back(versionEntry);
        }

        const std::vector<std::string> killProcesses = {
            "RoundTripApp.exe",
            "RoundTripHelper.exe"
        };

        InstallStateConfig installState;
        installState.mode = InstallStateMode::BOTH;
        installState.registryPath = "HKEY_CURRENT_USER\\Software\\RoundTripApp";
        installState.registryKey = "InstallState";
        installState.filePath = "%ProgramData%\\RoundTripApp\\install.state";
        installState.useMutex = true;
        installState.mutexName = "Global\\RoundTripApp_Install";

        const std::string uninstallPath = "C:\\Program Files\\RoundTripApp \\u6D4B\\u8BD5\\uninstall.exe";
        const std::string language = "zh_CN";

        const bool writeA = writeManifest(
            manifestPathA.string(),
            appName,
            version,
            installDir,
            files,
            registry,
            killProcesses,
            false,
            true,
            installState,
            uninstallPath,
            language);
        AssertTrue(writeA, "writeManifest A failed");

        json manifestA;
        const bool readA = readManifest(manifestPathA.string(), manifestA);
        AssertTrue(readA, "readManifest A failed");

        ValidateManifestFields(manifestA,
                               appName,
                               version,
                               installDir,
                               files,
                               registry,
                               killProcesses,
                               installState,
                               uninstallPath,
                               language);

        const auto filesRt = manifestA.at("files").get<std::vector<std::string>>();
        const auto killRt = manifestA.at("killProcesses").get<std::vector<std::string>>();
        const auto registryRt = ParseRegistry(manifestA);
        const auto installStateRt = ParseInstallState(manifestA);

        const bool writeB = writeManifest(
            manifestPathB.string(),
            manifestA.at("appName").get<std::string>(),
            manifestA.at("configVersion").get<std::string>(),
            manifestA.at("installDir").get<std::string>(),
            filesRt,
            registryRt,
            killRt,
            manifestA.at("autoStartup").get<bool>(),
            manifestA.at("desktopIcons").get<bool>(),
            installStateRt,
            manifestA.at("uninstallPath").get<std::string>(),
            manifestA.at("language").get<std::string>());
        AssertTrue(writeB, "writeManifest B failed");

        json manifestB;
        const bool readB = readManifest(manifestPathB.string(), manifestB);
        AssertTrue(readB, "readManifest B failed");

        AssertTrue(manifestA == manifestB, "round-trip manifest JSON mismatch");

        std::filesystem::remove_all(tempRoot, ec);
        std::cout << "manifest round-trip test passed" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "manifest round-trip test failed: " << ex.what() << std::endl;
        return 1;
    }
}
