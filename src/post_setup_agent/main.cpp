#include "common/utf8_utils.h"

#include <Windows.h>
#include <WinHTTP.h>
#include <bcrypt.h>
#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace MultiThreadedInstaller {

namespace {

struct AgentOptions {
    std::string installDir;
    std::string appVersion;
    std::string configUrl;
    std::string statePath;
    std::string logPath;
};

struct EnvironmentEntry {
    std::string key;
    std::string value;
    int order = 0;
};

struct RemoteComponent {
    std::string id;
    std::string url;
    std::string sha256;
    std::string saveAs;
    std::string installDir;
    std::string args;
    bool wait = true;
    uint32_t timeoutSec = 1800;
};

struct RemoteProfile {
    std::vector<EnvironmentEntry> environment;
    std::vector<RemoteComponent> components;
};

struct RemoteConfig {
    int version = 0;
    std::vector<std::string> whitelist;
    RemoteProfile defaultProfile;
    std::optional<RemoteProfile> specialProfile;
};

class Logger {
public:
    explicit Logger(std::string path);
    void info(const std::string& message);
    void error(const std::string& message);

private:
    void write(const char* level, const std::string& message);

    std::ofstream file_;
};

struct ExpansionContext {
    std::string installDir;
    std::string appVersion;
    std::string componentInstallDir;
};

std::string ToLowerAscii(std::string value);
std::string NormalizePathForCompare(std::string value);
bool StartsWithNoCase(const std::string& value, const std::string& prefix);
std::optional<AgentOptions> ParseArgs(int argc, wchar_t** argv, std::string& error);
std::string ExpandEnvVars(const std::string& value);
std::string ReplaceAll(std::string text, const std::string& token, const std::string& value);
std::string ExpandTokens(std::string value, const ExpansionContext& context);
bool ReadFileBytes(const fs::path& path, std::vector<uint8_t>& out, std::string& error);
bool ComputeSha256(const std::vector<uint8_t>& data, std::string& out, std::string& error);
bool ComputeFileSha256(const fs::path& path, std::string& out, std::string& error);
bool EnsureParentDirectory(const fs::path& path, std::string& error);
bool HttpGetBytes(const std::string& url, std::vector<uint8_t>& out, std::string& error);
bool FetchUrlToString(const std::string& url, std::string& out, std::string& error);
std::string FileUrlToPath(const std::string& url);
bool FetchComponentToPath(const std::string& url,
                          const fs::path& savePath,
                          const ExpansionContext& context,
                          std::string& error);
bool ParseEnvironmentEntry(const json& item, EnvironmentEntry& out, std::string& error);
bool ParseComponent(const json& item, RemoteComponent& out, std::string& error);
bool ParseProfile(const json& node, RemoteProfile& out, std::string& error);
bool ParseRemoteConfig(const std::string& payload, RemoteConfig& out, std::string& error);
const RemoteProfile& SelectProfile(const RemoteConfig& config,
                                   const std::string& appVersion,
                                   std::string& profileName);
bool ReadRegistryString(HKEY root,
                        const std::wstring& subKey,
                        const std::wstring& valueName,
                        std::wstring& out);
bool WriteRegistryString(HKEY root,
                         const std::wstring& subKey,
                         const std::wstring& valueName,
                         const std::wstring& value);
std::vector<std::wstring> SplitPathList(const std::wstring& value);
std::wstring JoinPathList(const std::vector<std::wstring>& parts);
bool ApplyEnvironmentEntries(const std::vector<EnvironmentEntry>& entries,
                             const ExpansionContext& context,
                             std::vector<EnvironmentEntry>& applied,
                             std::string& error);
bool ExecuteComponentInstaller(const fs::path& executablePath,
                               const std::string& args,
                               const std::string& componentInstallDir,
                               bool wait,
                               uint32_t timeoutSec,
                               DWORD& exitCode,
                               std::string& error);
bool WriteStateFile(const AgentOptions& options,
                    int configVersion,
                    const std::string& profile,
                    const std::vector<EnvironmentEntry>& appliedEnv,
                    const std::map<std::string, std::string>& installedComponents,
                    std::string& error);

} // namespace

namespace {

Logger::Logger(std::string path) {
    if (!path.empty()) {
        fs::path logPath = PathFromUtf8(path);
        std::error_code ec;
        fs::create_directories(logPath.parent_path(), ec);
        file_.open(logPath, std::ios::app | std::ios::binary);
    }
}

void Logger::info(const std::string& message) { write("INFO", message); }
void Logger::error(const std::string& message) { write("ERROR", message); }

void Logger::write(const char* level, const std::string& message) {
    const std::string line = "[" + std::string(level) + "] " + message + "\n";
    std::cerr << line;
    if (file_) {
        file_ << line;
        file_.flush();
    }
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string NormalizePathForCompare(std::string value) {
    std::replace(value.begin(), value.end(), '/', '\\');
    while (!value.empty() && (value.back() == '\\' || value.back() == '/')) {
        value.pop_back();
    }
    return ToLowerAscii(value);
}

bool StartsWithNoCase(const std::string& value, const std::string& prefix) {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::optional<AgentOptions> ParseArgs(int argc, wchar_t** argv, std::string& error) {
    AgentOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i] ? argv[i] : L"";
        auto consumeValue = [&](std::string& out) -> bool {
            if (i + 1 >= argc || !argv[i + 1]) {
                error = "Missing value for argument: " + WideToUtf8(arg);
                return false;
            }
            out = WideToUtf8(argv[++i]);
            return true;
        };

        if (arg == L"--install-dir") {
            if (!consumeValue(options.installDir)) return std::nullopt;
        } else if (arg == L"--app-version") {
            if (!consumeValue(options.appVersion)) return std::nullopt;
        } else if (arg == L"--config-url") {
            if (!consumeValue(options.configUrl)) return std::nullopt;
        } else if (arg == L"--state-path") {
            if (!consumeValue(options.statePath)) return std::nullopt;
        } else if (arg == L"--log-path") {
            if (!consumeValue(options.logPath)) return std::nullopt;
        }
    }

    if (options.installDir.empty() || options.appVersion.empty() ||
        options.configUrl.empty() || options.statePath.empty()) {
        error =
            "Required arguments: --install-dir --app-version --config-url --state-path";
        return std::nullopt;
    }
    return options;
}

std::string ExpandEnvVars(const std::string& value) {
    std::wstring input = Utf8ToWide(value);
    DWORD required = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (required == 0) {
        return value;
    }
    std::wstring output(required, L'\0');
    DWORD written = ExpandEnvironmentStringsW(input.c_str(), output.data(), required);
    if (written == 0) {
        return value;
    }
    if (!output.empty() && output.back() == L'\0') {
        output.pop_back();
    }
    return WideToUtf8(output);
}

std::string ReplaceAll(std::string text, const std::string& token, const std::string& value) {
    if (token.empty() || value.empty()) {
        return text;
    }
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
        text.replace(pos, token.size(), value);
        pos += value.size();
    }
    return text;
}

std::string ExpandTokens(std::string value, const ExpansionContext& context) {
    value = ReplaceAll(std::move(value), "%InstallDir%", context.installDir);
    value = ReplaceAll(std::move(value), "%AppVersion%", context.appVersion);
    value = ReplaceAll(std::move(value), "%ComponentInstallDir%", context.componentInstallDir);
    return ExpandEnvVars(value);
}

bool ReadFileBytes(const fs::path& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Failed to open file: " + Utf8FromPath(path);
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool ComputeSha256(const std::vector<uint8_t>& data, std::string& out, std::string& error) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD hashSize = 0;
    DWORD cbData = 0;
    std::vector<uint8_t> objectBuffer;
    std::vector<uint8_t> hashBuffer;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        error = "Failed to open SHA256 provider.";
        return false;
    }
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                          sizeof(objectSize), &cbData, 0) != 0 ||
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize),
                          sizeof(hashSize), &cbData, 0) != 0) {
        error = "Failed to query SHA256 properties.";
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    objectBuffer.resize(objectSize);
    hashBuffer.resize(hashSize);
    if (BCryptCreateHash(alg, &hash, objectBuffer.data(), objectSize, nullptr, 0, 0) != 0 ||
        BCryptHashData(hash,
                       const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data.data())),
                       static_cast<ULONG>(data.size()),
                       0) != 0 ||
        BCryptFinishHash(hash, hashBuffer.data(), hashSize, 0) != 0) {
        error = "Failed to compute SHA256.";
        if (hash) {
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    std::ostringstream oss;
    oss << std::hex;
    for (uint8_t b : hashBuffer) {
        oss.width(2);
        oss.fill('0');
        oss << static_cast<int>(b);
    }
    out = ToLowerAscii(oss.str());
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return true;
}

bool ComputeFileSha256(const fs::path& path, std::string& out, std::string& error) {
    std::vector<uint8_t> data;
    if (!ReadFileBytes(path, data, error)) {
        return false;
    }
    return ComputeSha256(data, out, error);
}

bool EnsureParentDirectory(const fs::path& path, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Failed to create directory: " + Utf8FromPath(path.parent_path());
        return false;
    }
    return true;
}

bool HttpGetBytes(const std::string& url, std::vector<uint8_t>& out, std::string& error) {
    std::wstring urlW = Utf8ToWide(url);
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(urlW.c_str(), 0, 0, &components)) {
        error = "Invalid URL: " + url;
        return false;
    }

    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }

    HINTERNET session = WinHttpOpen(L"post_setup_agent/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) {
        error = "Failed to initialize WinHTTP.";
        return false;
    }
    HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    HINTERNET request = nullptr;
    bool success = false;

    if (connection) {
        DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connection,
                                     L"GET",
                                     path.c_str(),
                                     nullptr,
                                     WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     flags);
    }
    if (!request) {
        error = "Failed to create HTTP request.";
    } else if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
               !WinHttpReceiveResponse(request, nullptr)) {
        error = "HTTP request failed.";
    } else {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (!WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                 &statusCode,
                                 &statusSize,
                                 WINHTTP_NO_HEADER_INDEX) ||
            statusCode != 200) {
            error = "HTTP request returned status " + std::to_string(statusCode) + ".";
        } else {
            out.clear();
            success = true;
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) {
                    error = "Failed while reading HTTP response.";
                    success = false;
                    break;
                }
                if (available == 0) {
                    break;
                }
                size_t start = out.size();
                out.resize(start + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, out.data() + start, available, &read)) {
                    error = "Failed while reading HTTP response body.";
                    success = false;
                    break;
                }
                out.resize(start + read);
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return success;
}

bool FetchUrlToString(const std::string& url, std::string& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!HttpGetBytes(url, bytes, error)) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

std::string FileUrlToPath(const std::string& url) {
    std::string path = url.substr(strlen("file://"));
    if (!path.empty() && path[0] == '/' && path.size() > 2 &&
        std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':') {
        path.erase(path.begin());
    }
    std::replace(path.begin(), path.end(), '/', '\\');
    return path;
}

bool FetchComponentToPath(const std::string& url,
                          const fs::path& savePath,
                          const ExpansionContext& context,
                          std::string& error) {
    if (!EnsureParentDirectory(savePath, error)) {
        return false;
    }
    if (StartsWithNoCase(url, "file://")) {
        fs::path source = PathFromUtf8(ExpandTokens(FileUrlToPath(url), context));
        std::error_code ec;
        fs::copy_file(source, savePath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Failed to copy local payload: " + Utf8FromPath(source);
            return false;
        }
        return true;
    }

    std::vector<uint8_t> bytes;
    if (!HttpGetBytes(url, bytes, error)) {
        return false;
    }
    std::ofstream out(savePath, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Failed to open download target: " + Utf8FromPath(savePath);
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool ParseEnvironmentEntry(const json& item, EnvironmentEntry& out, std::string& error) {
    if (!item.is_object() || !item.contains("key") || !item["key"].is_string() ||
        !item.contains("value") || !item["value"].is_string()) {
        error = "Invalid environment entry.";
        return false;
    }
    out.key = item["key"].get<std::string>();
    out.value = item["value"].get<std::string>();
    if (item.contains("order")) {
        if (!item["order"].is_number_integer()) {
            error = "Environment order must be integer.";
            return false;
        }
        out.order = item["order"].get<int>();
        if (ToLowerAscii(out.key) == "path" && out.order != 0 && out.order != -1) {
            error = "Path order must be 0 or -1.";
            return false;
        }
    }
    return true;
}

bool ParseComponent(const json& item, RemoteComponent& out, std::string& error) {
    if (!item.is_object()) {
        error = "Invalid component entry.";
        return false;
    }
    const char* required[] = { "id", "url", "sha256", "saveAs", "installDir" };
    for (const char* key : required) {
        if (!item.contains(key) || !item[key].is_string() || item[key].get<std::string>().empty()) {
            error = std::string("Missing required component field: ") + key;
            return false;
        }
    }
    out.id = item["id"].get<std::string>();
    out.url = item["url"].get<std::string>();
    out.sha256 = ToLowerAscii(item["sha256"].get<std::string>());
    out.saveAs = item["saveAs"].get<std::string>();
    out.installDir = item["installDir"].get<std::string>();
    out.args = item.value("args", "");
    out.wait = item.value("wait", true);
    out.timeoutSec = item.value("timeoutSec", 1800U);
    return true;
}

bool ParseProfile(const json& node, RemoteProfile& out, std::string& error) {
    if (!node.is_object()) {
        error = "Profile must be object.";
        return false;
    }
    if (node.contains("environment")) {
        if (!node["environment"].is_array()) {
            error = "Profile environment must be array.";
            return false;
        }
        for (const auto& item : node["environment"]) {
            EnvironmentEntry entry;
            if (!ParseEnvironmentEntry(item, entry, error)) {
                return false;
            }
            out.environment.push_back(std::move(entry));
        }
    }
    if (node.contains("components")) {
        if (!node["components"].is_array()) {
            error = "Profile components must be array.";
            return false;
        }
        for (const auto& item : node["components"]) {
            RemoteComponent component;
            if (!ParseComponent(item, component, error)) {
                return false;
            }
            out.components.push_back(std::move(component));
        }
    }
    return true;
}

bool ParseRemoteConfig(const std::string& payload, RemoteConfig& out, std::string& error) {
    json root = json::parse(payload, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "Remote JSON payload is invalid.";
        return false;
    }
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        error = "Remote config missing integer version.";
        return false;
    }
    out.version = root["version"].get<int>();
    if (!root.contains("default")) {
        error = "Remote config missing default profile.";
        return false;
    }
    if (!ParseProfile(root["default"], out.defaultProfile, error)) {
        return false;
    }
    if (root.contains("special")) {
        RemoteProfile profile;
        if (!ParseProfile(root["special"], profile, error)) {
            return false;
        }
        out.specialProfile = std::move(profile);
    }
    if (root.contains("whitelist")) {
        if (!root["whitelist"].is_array()) {
            error = "whitelist must be array.";
            return false;
        }
        for (const auto& item : root["whitelist"]) {
            if (!item.is_string()) {
                error = "whitelist entries must be strings.";
                return false;
            }
            out.whitelist.push_back(item.get<std::string>());
        }
    }
    return true;
}

const RemoteProfile& SelectProfile(const RemoteConfig& config,
                                   const std::string& appVersion,
                                   std::string& profileName) {
    const bool matched =
        std::find(config.whitelist.begin(), config.whitelist.end(), appVersion) !=
        config.whitelist.end();
    if (matched && config.specialProfile.has_value()) {
        profileName = "special";
        return *config.specialProfile;
    }
    profileName = "default";
    return config.defaultProfile;
}

bool ReadRegistryString(HKEY root,
                        const std::wstring& subKey,
                        const std::wstring& valueName,
                        std::wstring& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    LONG status = RegQueryValueExW(key, valueName.c_str(), nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(key);
        return false;
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(key, valueName.c_str(), nullptr, &type,
                              reinterpret_cast<LPBYTE>(value.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    out = std::move(value);
    return true;
}

bool WriteRegistryString(HKEY root,
                         const std::wstring& subKey,
                         const std::wstring& valueName,
                         const std::wstring& value) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        &disposition) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD type = value.find(L'%') != std::wstring::npos ? REG_EXPAND_SZ : REG_SZ;
    const DWORD size = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    LONG status = RegSetValueExW(key, valueName.c_str(), 0, type,
                                 reinterpret_cast<const BYTE*>(value.c_str()), size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

std::vector<std::wstring> SplitPathList(const std::wstring& value) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(L';', start);
        if (end == std::wstring::npos) {
            end = value.size();
        }
        std::wstring part = value.substr(start, end - start);
        if (!part.empty()) {
            parts.push_back(std::move(part));
        }
        if (end == value.size()) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

std::wstring JoinPathList(const std::vector<std::wstring>& parts) {
    std::wstring joined;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            joined += L';';
        }
        joined += parts[i];
    }
    return joined;
}

bool ApplyEnvironmentEntries(const std::vector<EnvironmentEntry>& entries,
                             const ExpansionContext& context,
                             std::vector<EnvironmentEntry>& applied,
                             std::string& error) {
    const std::wstring envKey = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
    std::wstring currentPath;
    ReadRegistryString(HKEY_LOCAL_MACHINE, envKey, L"Path", currentPath);
    std::vector<std::wstring> pathParts = SplitPathList(currentPath);
    std::unordered_set<std::string> seen;
    for (const auto& part : pathParts) {
        seen.insert(NormalizePathForCompare(WideToUtf8(part)));
    }

    for (const auto& entry : entries) {
        std::string expandedValue = ExpandTokens(entry.value, context);
        if (expandedValue.empty()) {
            continue;
        }
        if (ToLowerAscii(entry.key) == "path") {
            const std::string normalized = NormalizePathForCompare(expandedValue);
            if (seen.find(normalized) == seen.end()) {
                seen.insert(normalized);
                std::wstring valueW = Utf8ToWide(expandedValue);
                if (entry.order == 0) {
                    pathParts.insert(pathParts.begin(), valueW);
                } else {
                    pathParts.push_back(valueW);
                }
            }
        } else {
            if (!WriteRegistryString(HKEY_LOCAL_MACHINE,
                                     envKey,
                                     Utf8ToWide(entry.key),
                                     Utf8ToWide(expandedValue))) {
                error = "Failed to write environment variable: " + entry.key;
                return false;
            }
        }
        EnvironmentEntry appliedEntry = entry;
        appliedEntry.value = expandedValue;
        applied.push_back(std::move(appliedEntry));
    }

    if (!WriteRegistryString(HKEY_LOCAL_MACHINE, envKey, L"Path", JoinPathList(pathParts))) {
        error = "Failed to update system PATH.";
        return false;
    }

    SendMessageTimeoutW(HWND_BROADCAST,
                        WM_SETTINGCHANGE,
                        0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG,
                        5000,
                        nullptr);
    return true;
}

bool ExecuteComponentInstaller(const fs::path& executablePath,
                               const std::string& args,
                               const std::string& componentInstallDir,
                               bool wait,
                               uint32_t timeoutSec,
                               DWORD& exitCode,
                               std::string& error) {
    std::wstring commandLine = L"\"" + executablePath.wstring() + L"\"";
    if (!args.empty()) {
        commandLine += L" ";
        commandLine += Utf8ToWide(args);
    }
    std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
    commandBuffer.push_back(L'\0');

    std::wstring workingDir = executablePath.parent_path().wstring();
    std::wstring installDirW = Utf8ToWide(componentInstallDir);
    LPWCH existingEnv = GetEnvironmentStringsW();
    std::vector<wchar_t> env;
    if (existingEnv) {
        LPWCH current = existingEnv;
        while (*current) {
            size_t len = wcslen(current);
            env.insert(env.end(), current, current + len + 1);
            current += len + 1;
        }
        FreeEnvironmentStringsW(existingEnv);
    }
    std::wstring extra = L"POST_SETUP_COMPONENT_INSTALL_DIR=" + installDirW;
    env.insert(env.end(), extra.begin(), extra.end());
    env.push_back(L'\0');
    env.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    BOOL started = CreateProcessW(nullptr,
                                  commandBuffer.data(),
                                  nullptr,
                                  nullptr,
                                  FALSE,
                                  CREATE_UNICODE_ENVIRONMENT,
                                  env.data(),
                                  workingDir.empty() ? nullptr : workingDir.c_str(),
                                  &startupInfo,
                                  &processInfo);
    if (!started) {
        error = "Failed to start installer: " + Utf8FromPath(executablePath);
        return false;
    }

    CloseHandle(processInfo.hThread);
    if (!wait) {
        exitCode = 0;
        CloseHandle(processInfo.hProcess);
        return true;
    }

    DWORD waitMs = timeoutSec == 0 ? INFINITE : timeoutSec * 1000;
    DWORD waitResult = WaitForSingleObject(processInfo.hProcess, waitMs);
    if (waitResult != WAIT_OBJECT_0) {
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hProcess);
        error = "Installer timed out or wait failed.";
        return false;
    }
    if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        CloseHandle(processInfo.hProcess);
        error = "Failed to read installer exit code.";
        return false;
    }
    CloseHandle(processInfo.hProcess);
    return true;
}

bool WriteStateFile(const AgentOptions& options,
                    int configVersion,
                    const std::string& profile,
                    const std::vector<EnvironmentEntry>& appliedEnv,
                    const std::map<std::string, std::string>& installedComponents,
                    std::string& error) {
    fs::path statePath = PathFromUtf8(options.statePath);
    if (!EnsureParentDirectory(statePath, error)) {
        return false;
    }
    json root;
    root["configVersion"] = configVersion;
    root["profile"] = profile;
    root["appVersion"] = options.appVersion;
    json env = json::array();
    for (const auto& entry : appliedEnv) {
        env.push_back({ { "key", entry.key }, { "value", entry.value }, { "order", entry.order } });
    }
    root["environment"] = std::move(env);
    json installed = json::object();
    for (const auto& component : installedComponents) {
        installed[component.first] = { { "sha256", component.second }, { "installed", true } };
    }
    root["installedComponents"] = std::move(installed);

    std::ofstream out(statePath, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Failed to open state file: " + options.statePath;
        return false;
    }
    const std::string payload = root.dump(2);
    out.write(payload.c_str(), static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(out);
}

} // namespace

int RunPostSetupAgentMain(int argc, wchar_t** argv) {
    std::string parseError;
    auto maybeOptions = ParseArgs(argc, argv, parseError);
    Logger logger(maybeOptions ? maybeOptions->logPath : std::string());
    if (!maybeOptions) {
        logger.error(parseError);
        return 1;
    }

    const AgentOptions options = *maybeOptions;
    logger.info("post_setup_agent started.");

    ExpansionContext context;
    context.installDir = options.installDir;
    context.appVersion = options.appVersion;

    std::string payload;
    std::string error;
    if (!FetchUrlToString(options.configUrl, payload, error)) {
        logger.error(error);
        return 1;
    }

    RemoteConfig config;
    if (!ParseRemoteConfig(payload, config, error)) {
        logger.error(error);
        return 1;
    }

    std::string profileName;
    const RemoteProfile& profile = SelectProfile(config, options.appVersion, profileName);
    logger.info("Selected profile: " + profileName);

    std::vector<EnvironmentEntry> appliedEnvironment;
    if (!ApplyEnvironmentEntries(profile.environment, context, appliedEnvironment, error)) {
        logger.error(error);
        return 1;
    }

    std::map<std::string, std::string> installedComponents;
    for (const auto& component : profile.components) {
        context.componentInstallDir = ExpandTokens(component.installDir, context);
        if (!context.componentInstallDir.empty()) {
            std::error_code ec;
            fs::create_directories(PathFromUtf8(context.componentInstallDir), ec);
        }

        const fs::path savePath = PathFromUtf8(ExpandTokens(component.saveAs, context));
        if (!FetchComponentToPath(component.url, savePath, context, error)) {
            logger.error("Component " + component.id + ": " + error);
            return 1;
        }

        std::string actualHash;
        if (!ComputeFileSha256(savePath, actualHash, error)) {
            logger.error("Component " + component.id + ": " + error);
            return 1;
        }
        if (actualHash != component.sha256) {
            logger.error("Component " + component.id + ": SHA256 mismatch.");
            return 1;
        }

        DWORD exitCode = 0;
        const std::string expandedArgs = ExpandTokens(component.args, context);
        if (!ExecuteComponentInstaller(savePath,
                                       expandedArgs,
                                       context.componentInstallDir,
                                       component.wait,
                                       component.timeoutSec,
                                       exitCode,
                                       error)) {
            logger.error("Component " + component.id + ": " + error);
            return 1;
        }
        if (component.wait && exitCode != 0) {
            logger.error("Component " + component.id +
                         ": installer exited with code " + std::to_string(exitCode));
            return 1;
        }
        installedComponents[component.id] = actualHash;
    }

    if (!WriteStateFile(options, config.version, profileName, appliedEnvironment, installedComponents, error)) {
        logger.error(error);
        return 1;
    }

    logger.info("post_setup_agent completed successfully.");
    return 0;
}

} // namespace MultiThreadedInstaller

int wmain(int argc, wchar_t** argv) {
    return MultiThreadedInstaller::RunPostSetupAgentMain(argc, argv);
}
