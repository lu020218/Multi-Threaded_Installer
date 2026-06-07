#include "common/package_manifest_codec.h"

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
    PayloadManifest = 3,
    Hooks = 7,
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

// Parse a JSON section into a DOM, but drop the contents of every "fileIndex" array
// (it becomes an empty array). This skips building DOM nodes for tens of thousands of
// per-file entries that only the install path needs — saving both parse and allocation
// cost when the caller (GUI startup) does not require fileIndex.
bool ParseJsonSectionSkipFileIndex(const std::vector<uint8_t>& bytes, json& out, std::string& error) {
    // Self-contained SAX -> DOM builder (only the public json_sax<json> interface), so it
    // is independent of nlohmann's internal DOM-parser template. Containers are completed
    // bottom-up (moved into the parent on end_*), avoiding any dangling pointers.
    struct FilterSax : public nlohmann::json_sax<json> {
        json& root;
        std::vector<json> stack;        // containers currently being built
        std::vector<std::string> keys;  // key under which stack[i] goes into its parent
        std::vector<bool> parentIsObj;  // whether stack[i]'s parent is an object
        std::string curKey;             // pending key for the top object
        bool pendingFileIndex = false;  // last key was "fileIndex"
        int swallowDepth = 0;           // >0: inside a fileIndex array being dropped

        explicit FilterSax(json& r) : root(r) {}

        void emit(json&& v) {
            if (stack.empty()) {
                root = std::move(v);
                return;
            }
            json& top = stack.back();
            if (top.is_object()) {
                top[curKey] = std::move(v);
            } else {
                top.push_back(std::move(v));
            }
        }

        bool beginContainer(json&& container) {
            const bool parentObj = !stack.empty() && stack.back().is_object();
            keys.push_back(parentObj ? curKey : std::string());
            parentIsObj.push_back(parentObj);
            stack.push_back(std::move(container));
            return true;
        }
        bool endContainer() {
            json done = std::move(stack.back());
            stack.pop_back();
            const std::string k = std::move(keys.back());
            keys.pop_back();
            const bool parentObj = parentIsObj.back();
            parentIsObj.pop_back();
            if (stack.empty()) {
                root = std::move(done);
            } else if (parentObj) {
                stack.back()[k] = std::move(done);
            } else {
                stack.back().push_back(std::move(done));
            }
            return true;
        }

        bool null() override { if (swallowDepth) return true; emit(json(nullptr)); return true; }
        bool boolean(bool v) override { if (swallowDepth) return true; emit(json(v)); return true; }
        bool number_integer(number_integer_t v) override { if (swallowDepth) return true; emit(json(v)); return true; }
        bool number_unsigned(number_unsigned_t v) override { if (swallowDepth) return true; emit(json(v)); return true; }
        bool number_float(number_float_t v, const string_t&) override { if (swallowDepth) return true; emit(json(v)); return true; }
        bool string(string_t& v) override { if (swallowDepth) return true; emit(json(v)); return true; }
        bool binary(binary_t& v) override { if (swallowDepth) return true; emit(json(v)); return true; }
        bool start_object(std::size_t) override {
            if (swallowDepth) { ++swallowDepth; return true; }
            pendingFileIndex = false;
            return beginContainer(json::object());
        }
        bool key(string_t& v) override {
            if (swallowDepth) return true;
            curKey = v;
            pendingFileIndex = (v == "fileIndex");
            return true;
        }
        bool end_object() override {
            if (swallowDepth) { --swallowDepth; return true; }
            return endContainer();
        }
        bool start_array(std::size_t) override {
            if (swallowDepth) { ++swallowDepth; return true; }
            if (pendingFileIndex) {
                pendingFileIndex = false;
                emit(json::array());  // drop contents: store empty array for "fileIndex"
                swallowDepth = 1;
                return true;
            }
            return beginContainer(json::array());
        }
        bool end_array() override {
            if (swallowDepth) { --swallowDepth; return true; }
            return endContainer();
        }
        bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
            return false;
        }
    };

    try {
        FilterSax sax(out);
        const bool ok = json::sax_parse(bytes.begin(), bytes.end(), &sax,
                                        json::input_format_t::json, false);
        if (!ok) {
            error = "Invalid manifest JSON section (fileIndex-skip).";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        error = std::string("Invalid manifest JSON section: ") + ex.what();
        return false;
    }
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
                {"contentHash", file.contentHash},
                {"frameOffset", file.frameOffset},
                {"frameCompressedSize", file.frameCompressedSize},
            });
        }
        folders.push_back({
            {"folderId", folder.folderId},
            {"folderName", folder.folderName},
            {"source", folder.source},
            {"target", folder.target},
            {"required", folder.required},
            {"offset", folder.offset},
            {"compressedSize", folder.compressedSize},
            {"originalSize", folder.originalSize},
            {"checksum", folder.checksum},
            {"algorithm", static_cast<int>(folder.algorithm)},
            {"framed", folder.framed},
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
        folder.source = item.value("source", "");
        folder.target = item.value("target", "");
        folder.required = item.value("required", false);
        folder.offset = item.value("offset", 0ULL);
        folder.compressedSize = item.value("compressedSize", 0ULL);
        folder.originalSize = item.value("originalSize", 0ULL);
        folder.checksum = item.value("checksum", 0u);
        folder.algorithm = static_cast<CompressionAlgorithm>(item.value("algorithm", 0));
        folder.framed = item.value("framed", false);
        for (const auto& fileJson : item.value("fileIndex", json::array())) {
            FileIndexEntry file;
            file.relativePath = fileJson.value("relativePath", "");
            file.offset = fileJson.value("offset", 0ULL);
            file.size = fileJson.value("size", 0ULL);
            file.contentHash = fileJson.value("contentHash", 0ULL);
            file.frameOffset = fileJson.value("frameOffset", 0ULL);
            file.frameCompressedSize = fileJson.value("frameCompressedSize", 0ULL);
            folder.fileIndex.push_back(std::move(file));
        }
        payload.folders.push_back(std::move(folder));
    }
    return payload;
}

json HookToJson(const PackageHook& hook) {
    // 脚本内容以字节数组形式内嵌（脚本通常很小）。
    json content = json::array();
    for (uint8_t byte : hook.content) {
        content.push_back(static_cast<unsigned>(byte));
    }
    return json{
        {"present", hook.present},
        {"scriptName", hook.scriptName},
        {"content", content},
        {"args", hook.args},
        {"onFailure", static_cast<int>(hook.onFailure)},
        {"timeoutSec", hook.timeoutSec},
    };
}

PackageHook HookFromJson(const json& value) {
    PackageHook hook;
    hook.present = value.value("present", false);
    hook.scriptName = value.value("scriptName", "");
    if (value.contains("content") && value["content"].is_array()) {
        hook.content.reserve(value["content"].size());
        for (const auto& byte : value["content"]) {
            hook.content.push_back(static_cast<uint8_t>(byte.get<unsigned>()));
        }
    }
    hook.args = value.value("args", "");
    hook.onFailure = static_cast<HookOnFailure>(value.value("onFailure", 0));
    hook.timeoutSec = value.value("timeoutSec", 300u);
    return hook;
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
        {"productName", manifest.identity.productName},
        {"publisher", manifest.identity.publisher},
        {"version", manifest.identity.version},
        {"defaultDir", manifest.identity.defaultDir},
        {"copyright", manifest.identity.copyright},
    });
    AddSection(sections, SectionType::PayloadManifest, PayloadToJson(manifest.payload));
    AddSection(sections, SectionType::Hooks, {
        {"preInstall", HookToJson(manifest.hooks.preInstall)},
        {"postInstall", HookToJson(manifest.hooks.postInstall)},
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
                                std::string& error,
                                bool deferFileIndex) {
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
        const bool skipFileIndex =
            deferFileIndex && entry.type == static_cast<uint32_t>(SectionType::PayloadManifest);
        const bool parsed_ok = skipFileIndex
                                   ? ParseJsonSectionSkipFileIndex(bytes, parsed, error)
                                   : ParseJsonSection(bytes, parsed, error);
        if (!parsed_ok) {
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
    json payload;
    json hooks;
    if (!requireSection(SectionType::Identity, identity) ||
        !requireSection(SectionType::PayloadManifest, payload) ||
        !requireSection(SectionType::Hooks, hooks)) {
        return false;
    }

    manifest.version = header.version;
    manifest.identity.productName = identity.value("productName", "");
    manifest.identity.publisher = identity.value("publisher", "");
    manifest.identity.version = identity.value("version", "");
    manifest.identity.defaultDir = identity.value("defaultDir", "");
    manifest.identity.copyright = identity.value("copyright", "");

    manifest.payload = PayloadFromJson(payload);

    manifest.hooks.preInstall = HookFromJson(hooks.value("preInstall", json::object()));
    manifest.hooks.postInstall = HookFromJson(hooks.value("postInstall", json::object()));
    return true;
}

} // namespace MultiThreadedInstaller
