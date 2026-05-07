#include "common/package_manifest_codec.h"

#include <algorithm>
#include <cstring>
#include <json.hpp>
#include <unordered_map>

namespace MultiThreadedInstaller {
namespace {

using json = nlohmann::json;

constexpr uint32_t kPackageManifestMagic = 0x32464D50; // PMF2
constexpr uint32_t kSectionVersion = 1;

enum class SectionType : uint32_t {
    Identity = 1,
    InstallPolicy = 2,
    PayloadManifest = 3,
    ComponentManifest = 4,
    UiManifest = 5,
    LifecyclePolicy = 6,
};

struct PackageManifestHeader {
    uint32_t magic = kPackageManifestMagic;
    uint32_t version = Constants::VERSION;
    uint32_t sectionCount = 0;
    uint64_t directoryOffset = 0;
    uint64_t directorySize = 0;
};

struct PackageManifestSectionEntry {
    uint32_t type = 0;
    uint32_t version = kSectionVersion;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t checksum = 0;
};

template <typename T>
void AppendPod(std::vector<uint8_t>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
bool ReadPod(const std::vector<uint8_t>& data, size_t& offset, T& out) {
    if (offset + sizeof(T) > data.size()) {
        return false;
    }
    std::memcpy(&out, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

uint32_t Fnv1a(const std::vector<uint8_t>& data) {
    uint32_t hash = 2166136261u;
    for (uint8_t byte : data) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

std::vector<uint8_t> DumpJson(const json& value) {
    std::string text = value.dump();
    return std::vector<uint8_t>(text.begin(), text.end());
}

bool ParseJsonSection(const std::vector<uint8_t>& bytes, json& out, std::string& error) {
    try {
        out = json::parse(bytes.begin(), bytes.end());
        return true;
    } catch (const std::exception& ex) {
        error = std::string("Invalid manifest JSON section: ") + ex.what();
        return false;
    }
}

json RegistryToJson(const RegistryEntry& entry) {
    return json{
        {"path", entry.path},
        {"key", entry.key},
        {"value", entry.value},
        {"type", static_cast<int>(entry.type)},
    };
}

RegistryEntry RegistryFromJson(const json& value) {
    RegistryEntry entry;
    entry.path = value.value("path", "");
    entry.key = value.value("key", "");
    entry.value = value.value("value", "");
    entry.type = static_cast<RegistryValueType>(value.value("type", 0));
    return entry;
}

json RegistryListToJson(const std::vector<RegistryEntry>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        out.push_back(RegistryToJson(entry));
    }
    return out;
}

std::vector<RegistryEntry> RegistryListFromJson(const json& value) {
    std::vector<RegistryEntry> entries;
    if (!value.is_array()) {
        return entries;
    }
    for (const auto& item : value) {
        entries.push_back(RegistryFromJson(item));
    }
    return entries;
}

json InstallInfoToJson(const InstallInfoConfig& info) {
    json values = json::object();
    std::vector<std::pair<std::string, InstallInfoValueConfig>> ordered(info.values.begin(),
                                                                        info.values.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    for (const auto& item : ordered) {
        values[item.first] = {
            {"key", item.second.key},
            {"value", item.second.value},
            {"type", static_cast<int>(item.second.type)},
        };
    }
    return json{
        {"mode", static_cast<int>(info.mode)},
        {"path", info.path},
        {"values", values},
    };
}

InstallInfoConfig InstallInfoFromJson(const json& value) {
    InstallInfoConfig info;
    info.mode = static_cast<InstallStateMode>(value.value("mode", 0));
    info.path = value.value("path", "");
    if (value.contains("values") && value["values"].is_object()) {
        for (auto it = value["values"].begin(); it != value["values"].end(); ++it) {
            InstallInfoValueConfig entry;
            entry.key = it.value().value("key", "");
            entry.value = it.value().value("value", "");
            entry.type = static_cast<RegistryValueType>(it.value().value("type", 0));
            info.values[it.key()] = std::move(entry);
        }
    }
    return info;
}

json NamedListToJson(const std::vector<NamedCleanupEntry>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        out.push_back({{"name", entry.name}});
    }
    return out;
}

std::vector<NamedCleanupEntry> NamedListFromJson(const json& value) {
    std::vector<NamedCleanupEntry> entries;
    if (!value.is_array()) {
        return entries;
    }
    for (const auto& item : value) {
        NamedCleanupEntry entry;
        entry.name = item.value("name", "");
        entries.push_back(std::move(entry));
    }
    return entries;
}

json UninstallEntriesToJson(const std::vector<UninstallEntryCleanup>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        out.push_back({{"name", entry.name}, {"scope", static_cast<int>(entry.scope)}});
    }
    return out;
}

std::vector<UninstallEntryCleanup> UninstallEntriesFromJson(const json& value) {
    std::vector<UninstallEntryCleanup> entries;
    if (!value.is_array()) {
        return entries;
    }
    for (const auto& item : value) {
        UninstallEntryCleanup entry;
        entry.name = item.value("name", "");
        entry.scope = static_cast<UninstallEntryScope>(item.value("scope", 3));
        entries.push_back(std::move(entry));
    }
    return entries;
}

json CleanupRulesToJson(const std::vector<UninstallCleanupRule>& rules) {
    json out = json::array();
    for (const auto& rule : rules) {
        out.push_back({
            {"path", rule.path},
            {"recursive", rule.recursive},
            {"onlyIfEmpty", rule.onlyIfEmpty},
        });
    }
    return out;
}

std::vector<UninstallCleanupRule> CleanupRulesFromJson(const json& value) {
    std::vector<UninstallCleanupRule> rules;
    if (!value.is_array()) {
        return rules;
    }
    for (const auto& item : value) {
        UninstallCleanupRule rule;
        rule.path = item.value("path", "");
        rule.recursive = item.value("recursive", true);
        rule.onlyIfEmpty = item.value("onlyIfEmpty", false);
        rules.push_back(std::move(rule));
    }
    return rules;
}

json RegistryLookupsToJson(const std::vector<RegistryLookupEntry>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        out.push_back({{"path", entry.path}, {"key", entry.key}});
    }
    return out;
}

std::vector<RegistryLookupEntry> RegistryLookupsFromJson(const json& value) {
    std::vector<RegistryLookupEntry> entries;
    if (!value.is_array()) {
        return entries;
    }
    for (const auto& item : value) {
        RegistryLookupEntry entry;
        entry.path = item.value("path", "");
        entry.key = item.value("key", "");
        entries.push_back(std::move(entry));
    }
    return entries;
}

json UninstallCleanupToJson(const UninstallCleanupConfig& cleanup) {
    return json{
        {"processes", NamedListToJson(cleanup.processes)},
        {"registry", {{"legacyKeys", RegistryListToJson(cleanup.registry.legacyKeys)}}},
        {"uninstallEntries", UninstallEntriesToJson(cleanup.uninstallEntries)},
        {"shortcuts", NamedListToJson(cleanup.shortcuts)},
        {"startup", NamedListToJson(cleanup.startup)},
        {"paths", CleanupRulesToJson(cleanup.paths)},
    };
}

UninstallCleanupConfig UninstallCleanupFromJson(const json& value) {
    UninstallCleanupConfig cleanup;
    cleanup.processes = NamedListFromJson(value.value("processes", json::array()));
    if (value.contains("registry")) {
        cleanup.registry.legacyKeys =
            RegistryListFromJson(value["registry"].value("legacyKeys", json::array()));
    }
    cleanup.uninstallEntries = UninstallEntriesFromJson(value.value("uninstallEntries", json::array()));
    cleanup.shortcuts = NamedListFromJson(value.value("shortcuts", json::array()));
    cleanup.startup = NamedListFromJson(value.value("startup", json::array()));
    cleanup.paths = CleanupRulesFromJson(value.value("paths", json::array()));
    return cleanup;
}

json UpgradeCleanupToJson(const UpgradeCleanupConfig& cleanup) {
    return json{
        {"installRoots", RegistryLookupsToJson(cleanup.installRoots)},
        {"registry", {{"legacyKeys", RegistryListToJson(cleanup.registry.legacyKeys)}}},
        {"uninstallEntries", UninstallEntriesToJson(cleanup.uninstallEntries)},
        {"shortcuts", NamedListToJson(cleanup.shortcuts)},
        {"startup", NamedListToJson(cleanup.startup)},
        {"extraPaths", CleanupRulesToJson(cleanup.extraPaths)},
    };
}

UpgradeCleanupConfig UpgradeCleanupFromJson(const json& value) {
    UpgradeCleanupConfig cleanup;
    cleanup.installRoots = RegistryLookupsFromJson(value.value("installRoots", json::array()));
    if (value.contains("registry")) {
        cleanup.registry.legacyKeys =
            RegistryListFromJson(value["registry"].value("legacyKeys", json::array()));
    }
    cleanup.uninstallEntries = UninstallEntriesFromJson(value.value("uninstallEntries", json::array()));
    cleanup.shortcuts = NamedListFromJson(value.value("shortcuts", json::array()));
    cleanup.startup = NamedListFromJson(value.value("startup", json::array()));
    cleanup.extraPaths = CleanupRulesFromJson(value.value("extraPaths", json::array()));
    return cleanup;
}

json ComponentSourceToJson(const ComponentSourceConfig& source) {
    return json{
        {"type", static_cast<int>(source.type)},
        {"local", {
            {"base", source.local.base},
            {"installer", source.local.installer},
            {"args", source.local.args},
            {"wait", source.local.wait},
            {"timeoutSec", source.local.timeoutSec},
            {"uninstall", source.local.uninstall},
        }},
        {"download", {
            {"url", source.download.url},
            {"sha256", source.download.sha256},
            {"saveAs", source.download.saveAs},
            {"args", source.download.args},
            {"wait", source.download.wait},
            {"timeoutSec", source.download.timeoutSec},
            {"uninstall", source.download.uninstall},
        }},
    };
}

ComponentSourceConfig ComponentSourceFromJson(const json& value) {
    ComponentSourceConfig source;
    source.type = static_cast<ComponentSourceType>(value.value("type", 0));
    const json local = value.value("local", json::object());
    source.local.base = local.value("base", "");
    source.local.installer = local.value("installer", "");
    source.local.args = local.value("args", "");
    source.local.wait = local.value("wait", true);
    source.local.timeoutSec = local.value("timeoutSec", 900u);
    source.local.uninstall = local.value("uninstall", "");
    const json download = value.value("download", json::object());
    source.download.url = download.value("url", "");
    source.download.sha256 = download.value("sha256", "");
    source.download.saveAs = download.value("saveAs", "");
    source.download.args = download.value("args", "");
    source.download.wait = download.value("wait", true);
    source.download.timeoutSec = download.value("timeoutSec", 1800u);
    source.download.uninstall = download.value("uninstall", "");
    return source;
}

json ComponentsToJson(const std::vector<ComponentConfig>& components) {
    json out = json::array();
    for (const auto& component : components) {
        out.push_back({
            {"id", component.id},
            {"name", component.name},
            {"description", component.description},
            {"required", component.required},
            {"defaultSelected", component.defaultSelected},
            {"sizeHintMB", component.sizeHintMB},
            {"dependsOn", component.dependsOn},
            {"folders", component.folders},
            {"source", ComponentSourceToJson(component.source)},
            {"registry", RegistryListToJson(component.registry)},
            {"killProcesses", component.killProcesses},
            {"createDesktopShortcut", component.createDesktopShortcut},
            {"autoStartup", component.autoStartup},
        });
    }
    return out;
}

std::vector<ComponentConfig> ComponentsFromJson(const json& value) {
    std::vector<ComponentConfig> components;
    if (!value.is_array()) {
        return components;
    }
    for (const auto& item : value) {
        ComponentConfig component;
        component.id = item.value("id", "");
        component.name = item.value("name", "");
        component.description = item.value("description", "");
        component.required = item.value("required", false);
        component.defaultSelected = item.value("defaultSelected", true);
        component.sizeHintMB = item.value("sizeHintMB", 0u);
        component.dependsOn = item.value("dependsOn", std::vector<std::string>{});
        component.folders = item.value("folders", std::vector<std::string>{});
        component.source = ComponentSourceFromJson(item.value("source", json::object()));
        component.registry = RegistryListFromJson(item.value("registry", json::array()));
        component.killProcesses = item.value("killProcesses", std::vector<std::string>{});
        component.createDesktopShortcut = item.value("createDesktopShortcut", false);
        component.autoStartup = item.value("autoStartup", false);
        components.push_back(std::move(component));
    }
    return components;
}

json UiToJson(const PackageUiManifest& ui) {
    json pages = json::array();
    for (const auto& page : ui.componentSelection.pages) {
        pages.push_back({{"skin", page.skin}, {"controls", page.controls}});
    }
    json links = json::array();
    for (const auto& link : ui.links) {
        links.push_back({{"control", link.control}, {"url", link.url}});
    }
    return json{
        {"desktopShortcutDefaultName", ui.desktopShortcutDefaultName},
        {"desktopShortcutLocalizedNames", ui.desktopShortcutLocalizedNames},
        {"componentSelection", {
            {"mode", ui.componentSelection.mode},
            {"strategy", ui.componentSelection.strategy},
            {"tokenPrefix", ui.componentSelection.tokenPrefix},
            {"pages", pages},
        }},
        {"links", links},
    };
}

PackageUiManifest UiFromJson(const json& value) {
    PackageUiManifest ui;
    ui.desktopShortcutDefaultName = value.value("desktopShortcutDefaultName", "");
    ui.desktopShortcutLocalizedNames =
        value.value("desktopShortcutLocalizedNames", std::unordered_map<std::string, std::string>{});
    const json selection = value.value("componentSelection", json::object());
    ui.componentSelection.mode = selection.value("mode", "dedicatedPage");
    ui.componentSelection.strategy = selection.value("strategy", "xml_userdata");
    ui.componentSelection.tokenPrefix = selection.value("tokenPrefix", "component:");
    for (const auto& item : selection.value("pages", json::array())) {
        UiComponentBindingPage page;
        page.skin = item.value("skin", "");
        page.controls = item.value("controls", std::vector<std::string>{});
        ui.componentSelection.pages.push_back(std::move(page));
    }
    for (const auto& item : value.value("links", json::array())) {
        UiLinkBinding link;
        link.control = item.value("control", "");
        link.url = item.value("url", "");
        ui.links.push_back(std::move(link));
    }
    return ui;
}

json PayloadToJson(const PackagePayloadManifest& payload) {
    json folders = json::array();
    for (const auto& folder : payload.folders) {
        json files = json::array();
        for (const auto& file : folder.fileIndex) {
            files.push_back({
                {"relativePath", file.relativePath},
                {"offset", file.offset},
                {"size", file.size},
            });
        }
        folders.push_back({
            {"folderId", folder.folderId},
            {"folderName", folder.folderName},
            {"target", folder.target},
            {"offset", folder.offset},
            {"compressedSize", folder.compressedSize},
            {"originalSize", folder.originalSize},
            {"checksum", folder.checksum},
            {"algorithm", static_cast<int>(folder.algorithm)},
            {"fileIndex", files},
        });
    }
    return json{{"totalCompressedSize", payload.totalCompressedSize}, {"folders", folders}};
}

PackagePayloadManifest PayloadFromJson(const json& value) {
    PackagePayloadManifest payload;
    payload.totalCompressedSize = value.value("totalCompressedSize", 0ULL);
    for (const auto& item : value.value("folders", json::array())) {
        PackagePayloadFolder folder;
        folder.folderId = item.value("folderId", "");
        folder.folderName = item.value("folderName", "");
        folder.target = item.value("target", "");
        folder.offset = item.value("offset", 0ULL);
        folder.compressedSize = item.value("compressedSize", 0ULL);
        folder.originalSize = item.value("originalSize", 0ULL);
        folder.checksum = item.value("checksum", 0u);
        folder.algorithm = static_cast<CompressionAlgorithm>(item.value("algorithm", 0));
        for (const auto& fileJson : item.value("fileIndex", json::array())) {
            FileIndexEntry file;
            file.relativePath = fileJson.value("relativePath", "");
            file.offset = fileJson.value("offset", 0ULL);
            file.size = fileJson.value("size", 0ULL);
            folder.fileIndex.push_back(std::move(file));
        }
        payload.folders.push_back(std::move(folder));
    }
    return payload;
}

void AddSection(std::vector<std::pair<SectionType, std::vector<uint8_t>>>& sections,
                SectionType type,
                const json& value) {
    sections.emplace_back(type, DumpJson(value));
}

} // namespace

std::vector<uint8_t> SerializePackageManifest(const PackageManifest& manifest) {
    std::vector<std::pair<SectionType, std::vector<uint8_t>>> sections;
    AddSection(sections, SectionType::Identity, {
        {"appName", manifest.identity.appName},
        {"appId", manifest.identity.appId},
        {"appDirectoryName", manifest.identity.appDirectoryName},
        {"appVersion", manifest.identity.appVersion},
        {"appWebsite", manifest.identity.appWebsite},
    });
    AddSection(sections, SectionType::InstallPolicy, {
        {"defaultDir", manifest.install.defaultDir},
        {"autoStartup", manifest.install.autoStartup},
        {"desktopIcon", manifest.install.desktopIcon},
        {"autoCleanOldInstall", manifest.install.autoCleanOldInstall},
        {"requireAdmin", manifest.install.requireAdmin},
        {"minWindowsMajor", manifest.install.minWindowsMajor},
        {"minWindowsMinor", manifest.install.minWindowsMinor},
        {"minWindowsBuild", manifest.install.minWindowsBuild},
        {"sparseFileThresholdBytes", manifest.install.sparseFileThresholdBytes},
        {"useMutex", manifest.install.useMutex},
        {"mutexName", manifest.install.mutexName},
        {"installInfo", InstallInfoToJson(manifest.install.installInfo)},
        {"installRegistry", RegistryListToJson(manifest.install.installRegistry)},
        {"killProcesses", manifest.install.killProcesses},
    });
    AddSection(sections, SectionType::PayloadManifest, PayloadToJson(manifest.payload));
    AddSection(sections, SectionType::ComponentManifest,
               {{"components", ComponentsToJson(manifest.components.components)}});
    AddSection(sections, SectionType::UiManifest, UiToJson(manifest.ui));
    AddSection(sections, SectionType::LifecyclePolicy, {
        {"uninstallCleanup", UninstallCleanupToJson(manifest.lifecycle.uninstallCleanup)},
        {"upgradeCleanup", UpgradeCleanupToJson(manifest.lifecycle.upgradeCleanup)},
    });

    PackageManifestHeader header;
    header.version = Constants::VERSION;
    header.sectionCount = static_cast<uint32_t>(sections.size());
    header.directoryOffset = sizeof(PackageManifestHeader);
    header.directorySize = sizeof(PackageManifestSectionEntry) * sections.size();

    std::vector<uint8_t> out;
    AppendPod(out, header);
    const size_t directoryStart = out.size();
    out.resize(out.size() + static_cast<size_t>(header.directorySize));

    std::vector<PackageManifestSectionEntry> directory;
    uint64_t offset = static_cast<uint64_t>(sizeof(PackageManifestHeader) + header.directorySize);
    for (const auto& section : sections) {
        PackageManifestSectionEntry entry;
        entry.type = static_cast<uint32_t>(section.first);
        entry.offset = offset;
        entry.size = section.second.size();
        entry.checksum = Fnv1a(section.second);
        directory.push_back(entry);
        out.insert(out.end(), section.second.begin(), section.second.end());
        offset += section.second.size();
    }
    std::memcpy(out.data() + directoryStart, directory.data(),
                directory.size() * sizeof(PackageManifestSectionEntry));
    return out;
}

bool DeserializePackageManifest(const std::vector<uint8_t>& data,
                                PackageManifest& manifest,
                                std::string& error) {
    manifest = PackageManifest{};
    error.clear();
    size_t offset = 0;
    PackageManifestHeader header;
    if (!ReadPod(data, offset, header)) {
        error = "Package manifest header is truncated.";
        return false;
    }
    if (header.magic != kPackageManifestMagic) {
        error = "Unsupported package manifest format.";
        return false;
    }
    if (header.version != Constants::VERSION) {
        error = "Unsupported package manifest version.";
        return false;
    }
    if (header.directoryOffset + header.directorySize > data.size() ||
        header.directorySize != header.sectionCount * sizeof(PackageManifestSectionEntry)) {
        error = "Invalid package manifest section directory.";
        return false;
    }

    std::unordered_map<uint32_t, json> sectionJson;
    size_t dirOffset = static_cast<size_t>(header.directoryOffset);
    for (uint32_t i = 0; i < header.sectionCount; ++i) {
        PackageManifestSectionEntry entry;
        if (!ReadPod(data, dirOffset, entry)) {
            error = "Package manifest section directory is truncated.";
            return false;
        }
        if (entry.version != kSectionVersion ||
            entry.offset + entry.size > data.size()) {
            error = "Invalid package manifest section entry.";
            return false;
        }
        std::vector<uint8_t> bytes(data.begin() + static_cast<std::ptrdiff_t>(entry.offset),
                                   data.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.size));
        if (Fnv1a(bytes) != entry.checksum) {
            error = "Package manifest section checksum mismatch.";
            return false;
        }
        json parsed;
        if (!ParseJsonSection(bytes, parsed, error)) {
            return false;
        }
        sectionJson[entry.type] = std::move(parsed);
    }

    auto requireSection = [&](SectionType type, json& out) -> bool {
        auto it = sectionJson.find(static_cast<uint32_t>(type));
        if (it == sectionJson.end()) {
            error = "Package manifest required section is missing.";
            return false;
        }
        out = it->second;
        return true;
    };

    json identity;
    json install;
    json payload;
    json components;
    json ui;
    json lifecycle;
    if (!requireSection(SectionType::Identity, identity) ||
        !requireSection(SectionType::InstallPolicy, install) ||
        !requireSection(SectionType::PayloadManifest, payload) ||
        !requireSection(SectionType::ComponentManifest, components) ||
        !requireSection(SectionType::UiManifest, ui) ||
        !requireSection(SectionType::LifecyclePolicy, lifecycle)) {
        return false;
    }

    manifest.version = header.version;
    manifest.identity.appName = identity.value("appName", "");
    manifest.identity.appId = identity.value("appId", "");
    manifest.identity.appDirectoryName = identity.value("appDirectoryName", "");
    manifest.identity.appVersion = identity.value("appVersion", "");
    manifest.identity.appWebsite = identity.value("appWebsite", "");

    manifest.install.defaultDir = install.value("defaultDir", "");
    manifest.install.autoStartup = install.value("autoStartup", false);
    manifest.install.desktopIcon = install.value("desktopIcon", false);
    manifest.install.autoCleanOldInstall = install.value("autoCleanOldInstall", false);
    manifest.install.requireAdmin = install.value("requireAdmin", false);
    manifest.install.minWindowsMajor =
        static_cast<uint16_t>(install.value("minWindowsMajor", 0));
    manifest.install.minWindowsMinor =
        static_cast<uint16_t>(install.value("minWindowsMinor", 0));
    manifest.install.minWindowsBuild = install.value("minWindowsBuild", 0u);
    manifest.install.sparseFileThresholdBytes = install.value("sparseFileThresholdBytes", 0ULL);
    manifest.install.useMutex = install.value("useMutex", true);
    manifest.install.mutexName = install.value("mutexName", "");
    manifest.install.installInfo = InstallInfoFromJson(install.value("installInfo", json::object()));
    manifest.install.installRegistry = RegistryListFromJson(install.value("installRegistry", json::array()));
    manifest.install.killProcesses = install.value("killProcesses", std::vector<std::string>{});

    manifest.payload = PayloadFromJson(payload);
    manifest.components.components =
        ComponentsFromJson(components.value("components", json::array()));
    manifest.ui = UiFromJson(ui);
    manifest.lifecycle.uninstallCleanup =
        UninstallCleanupFromJson(lifecycle.value("uninstallCleanup", json::object()));
    manifest.lifecycle.upgradeCleanup =
        UpgradeCleanupFromJson(lifecycle.value("upgradeCleanup", json::object()));
    return true;
}

} // namespace MultiThreadedInstaller
