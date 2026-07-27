#include "common/package_manifest_codec.h"

#include <cstring>
#include <json.hpp>
#include <unordered_map>

#ifdef ZSTD_FOUND
#include <zstd.h>
#endif

namespace MultiThreadedInstaller {
namespace {

using json = nlohmann::json;

constexpr uint32_t kPackageManifestMagic = 0x32464D50; // PMF2
// 压缩元数据包裹的魔数（须与 kPackageManifestMagic 不同）：
// 布局 = [kCompressedMetaMagic(4)] [uncompressedSize(8)] [zstd 压缩字节...]。
// 反序列化时若检测到该魔数则先整体解压再按 PMF2 解析；否则按未压缩 PMF2 直接解析（向后兼容）。
constexpr uint32_t kCompressedMetaMagic = 0x315A4D43; // "CMZ1"
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
            // 运行期非分帧模式只用到 relativePath/size/contentHash；offset 与 frame* 仅分帧时
            // 解压需要，故只在 framed 时写出，避免给每个文件平白多塞几十字节未压缩 JSON。
            json entry = {
                {"relativePath", file.relativePath},
                {"size", file.size},
                {"contentHash", file.contentHash},
            };
            if (folder.framed) {
                entry["offset"] = file.offset;
                entry["frameOffset"] = file.frameOffset;
                entry["frameCompressedSize"] = file.frameCompressedSize;
            }
            files.push_back(std::move(entry));
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

json BytesToJson(const std::vector<uint8_t>& bytes) {
    json arr = json::array();
    for (uint8_t byte : bytes) {
        arr.push_back(static_cast<unsigned>(byte));
    }
    return arr;
}

void BytesFromJson(const json& value, const char* key, std::vector<uint8_t>& out) {
    if (value.contains(key) && value[key].is_array()) {
        out.reserve(value[key].size());
        for (const auto& byte : value[key]) {
            out.push_back(static_cast<uint8_t>(byte.get<unsigned>()));
        }
    }
}

json HookToJson(const PackageHook& hook) {
    // 脚本内容以字节数组形式内嵌（脚本通常很小）。
    json auxFiles = json::array();
    for (const auto& aux : hook.auxFiles) {
        auxFiles.push_back(json{
            {"relativePath", aux.relativePath},
            {"content", BytesToJson(aux.content)},
        });
    }
    return json{
        {"present", hook.present},
        {"scriptName", hook.scriptName},
        {"content", BytesToJson(hook.content)},
        {"args", hook.args},
        {"onFailure", static_cast<int>(hook.onFailure)},
        {"timeoutSec", hook.timeoutSec},
        {"auxFiles", auxFiles},
        {"keep", hook.keep},
        {"keepDir", hook.keepDir},
    };
}

PackageHook HookFromJson(const json& value) {
    PackageHook hook;
    hook.present = value.value("present", false);
    hook.scriptName = value.value("scriptName", "");
    BytesFromJson(value, "content", hook.content);
    hook.args = value.value("args", "");
    hook.onFailure = static_cast<HookOnFailure>(value.value("onFailure", 0));
    hook.timeoutSec = value.value("timeoutSec", 300u);
    if (value.contains("auxFiles") && value["auxFiles"].is_array()) {
        hook.auxFiles.reserve(value["auxFiles"].size());
        for (const auto& item : value["auxFiles"]) {
            HookAuxFile aux;
            aux.relativePath = item.value("relativePath", "");
            BytesFromJson(item, "content", aux.content);
            if (!aux.relativePath.empty()) {
                hook.auxFiles.push_back(std::move(aux));
            }
        }
    }
    hook.keep = value.value("keep", false);
    hook.keepDir = value.value("keepDir", "");
    return hook;
}

void AddSection(std::vector<std::pair<SectionType, std::vector<uint8_t>>>& sections,
                SectionType type,
                const json& value) {
    sections.emplace_back(type, DumpJson(value));
}

// 把序列化好的 PMF2 字节整体压缩（zstd 一次性），加包裹头返回；体积无收益或无 zstd 时原样返回。
// 仅减小内嵌体积，运行期解压一次（zstd 解压极快），不影响载荷解压与安装流程。
std::vector<uint8_t> MaybeCompressMetaBlob(const std::vector<uint8_t>& raw) {
#ifdef ZSTD_FOUND
    if (raw.size() < 4096) {
        return raw;  // 太小不值得压缩（包裹头反而增重）
    }
    const size_t bound = ZSTD_compressBound(raw.size());
    std::vector<uint8_t> compressed(bound);
    const size_t produced =
        ZSTD_compress(compressed.data(), bound, raw.data(), raw.size(), 19);
    if (ZSTD_isError(produced) || produced + 12 >= raw.size()) {
        return raw;  // 压缩失败或没压下去 → 用原始
    }
    std::vector<uint8_t> out;
    out.reserve(12 + produced);
    AppendPod(out, kCompressedMetaMagic);
    AppendPod(out, static_cast<uint64_t>(raw.size()));
    out.insert(out.end(), compressed.begin(), compressed.begin() + produced);
    return out;
#else
    return raw;
#endif
}

// 若 input 带压缩包裹头则解压到 out 返回 true；否则不处理返回 false（调用方按未压缩处理）。
bool TryDecompressMetaBlob(const std::vector<uint8_t>& input,
                           std::vector<uint8_t>& out,
                           std::string& error) {
    if (input.size() < 4) {
        return false;
    }
    uint32_t magic = 0;
    std::memcpy(&magic, input.data(), sizeof(magic));
    if (magic != kCompressedMetaMagic) {
        return false;
    }
#ifdef ZSTD_FOUND
    if (input.size() < 12) {
        error = "Compressed metadata header is truncated.";
        return false;
    }
    uint64_t rawSize = 0;
    std::memcpy(&rawSize, input.data() + 4, sizeof(rawSize));
    out.resize(static_cast<size_t>(rawSize));
    const size_t produced = ZSTD_decompress(out.data(), out.size(),
                                            input.data() + 12, input.size() - 12);
    if (ZSTD_isError(produced) || produced != rawSize) {
        error = "Failed to decompress package metadata.";
        return false;
    }
    return true;
#else
    error = "Package metadata is compressed but ZSTD support is unavailable.";
    return false;
#endif
}

} // namespace

std::vector<uint8_t> SerializePackageManifest(const PackageManifest& manifest) {
    std::vector<std::pair<SectionType, std::vector<uint8_t>>> sections;
    AddSection(sections, SectionType::Identity, {
        {"productName", manifest.identity.productName},
        {"appName", manifest.identity.appName},
        {"appId", manifest.identity.appId},
        {"publisher", manifest.identity.publisher},
        {"version", manifest.identity.version},
        {"defaultDir", manifest.identity.defaultDir},
        {"copyright", manifest.identity.copyright},
    });
    AddSection(sections, SectionType::PayloadManifest, PayloadToJson(manifest.payload));
    json preInstallHooks = json::array();
    for (const auto& hook : manifest.hooks.preInstall) {
        preInstallHooks.push_back(HookToJson(hook));
    }
    json postInstallHooks = json::array();
    for (const auto& hook : manifest.hooks.postInstall) {
        postInstallHooks.push_back(HookToJson(hook));
    }
    AddSection(sections, SectionType::Hooks, {
        {"preInstall", std::move(preInstallHooks)},
        {"postInstall", std::move(postInstallHooks)},
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
    // 内嵌前整体压缩（zstd），显著减小元数据（尤其逐文件 fileIndex）的未压缩占用。
    return MaybeCompressMetaBlob(out);
}

bool DeserializePackageManifest(const std::vector<uint8_t>& input,
                                PackageManifest& manifest,
                                std::string& error,
                                bool deferFileIndex) {
    manifest = PackageManifest{};
    error.clear();

    // 自动检测压缩包裹头：带头则整体解压一次再按 PMF2 解析；否则按未压缩 PMF2 处理。
    std::vector<uint8_t> decompressed;
    const std::vector<uint8_t>* effective = &input;
    if (input.size() >= sizeof(uint32_t)) {
        uint32_t magic = 0;
        std::memcpy(&magic, input.data(), sizeof(magic));
        if (magic == kCompressedMetaMagic) {
            if (!TryDecompressMetaBlob(input, decompressed, error)) {
                return false;
            }
            effective = &decompressed;
        }
    }
    const std::vector<uint8_t>& data = *effective;

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
    manifest.identity.appName = identity.value("appName", "");
    manifest.identity.appId = identity.value("appId", "");
    manifest.identity.publisher = identity.value("publisher", "");
    manifest.identity.version = identity.value("version", "");
    manifest.identity.defaultDir = identity.value("defaultDir", "");
    manifest.identity.copyright = identity.value("copyright", "");

    manifest.payload = PayloadFromJson(payload);

    // preInstall / postInstall 为脚本数组（按顺序执行）。向后兼容单对象写法：
    // 若该字段是对象而非数组，按单元素列表解析。
    auto parseHookList = [](const json& node, std::vector<PackageHook>& out) {
        if (node.is_array()) {
            out.reserve(node.size());
            for (const auto& item : node) {
                out.push_back(HookFromJson(item));
            }
        } else if (node.is_object()) {
            out.push_back(HookFromJson(node));
        }
    };
    parseHookList(hooks.value("preInstall", json::array()), manifest.hooks.preInstall);
    parseHookList(hooks.value("postInstall", json::array()), manifest.hooks.postInstall);
    return true;
}

} // namespace MultiThreadedInstaller
