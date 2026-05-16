#include "common/utf8_utils.h"
#include "post_setup_agent/post_setup_url_utils.h"

#include <Windows.h>
#include <WinHTTP.h>
#include <bcrypt.h>
#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <thread>
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
    bool uninstallMode = false;
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
    void close();

private:
    void write(const char* level, const std::string& message);

    std::ofstream file_;
};

struct ExpansionContext {
    std::string installDir;
    std::string appVersion;
    std::string componentInstallDir;
};

struct PreparedEnvironmentChanges {
    std::vector<std::pair<std::wstring, std::wstring>> variables;
    std::wstring joinedPath;
    std::vector<EnvironmentEntry> appliedEntries;
};

struct PersistedState {
    std::vector<EnvironmentEntry> environment;
    std::string statePath;
    std::string logPath;
};

constexpr DWORD kHttpResolveTimeoutMs = 10000;
constexpr DWORD kHttpConnectTimeoutMs = 10000;
constexpr DWORD kHttpSendTimeoutMs = 15000;
constexpr DWORD kHttpReceiveTimeoutMs = 30000;
constexpr int kHttpMaxAttempts = 3;
constexpr DWORD kHttpRetryDelayMs = 1000;

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
bool HttpDownloadToSink(const std::string& url,
                       const std::function<bool(const uint8_t*, size_t)>& onChunk,
                       std::string& error);
bool HttpGetBytes(const std::string& url, std::vector<uint8_t>& out, std::string& error);
bool FetchUrlToString(const std::string& url, std::string& out, std::string& error);
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
bool DeleteRegistryValueIfPresent(HKEY root,
                                  const std::wstring& subKey,
                                  const std::wstring& valueName);
std::vector<std::wstring> SplitPathList(const std::wstring& value);
std::wstring JoinPathList(const std::vector<std::wstring>& parts);
std::vector<wchar_t> BuildCurrentEnvironmentBlock(
    const std::vector<std::pair<std::wstring, std::wstring>>& extraEntries);
bool PrepareEnvironmentChanges(const std::vector<EnvironmentEntry>& entries,
                               const ExpansionContext& context,
                               PreparedEnvironmentChanges& prepared,
                               std::string& error);
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
bool ReadStateFile(const std::string& statePath,
                   PersistedState& out,
                   bool& missing,
                   std::string& error);
bool RollbackEnvironmentEntries(const std::vector<EnvironmentEntry>& entries,
                                std::string& error);
bool DeleteFileIfExists(const fs::path& path, std::string& error);
bool RunPostSetupAgentInstall(const AgentOptions& options, Logger& logger);
bool RunPostSetupAgentUninstall(const AgentOptions& options, Logger& logger);

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
void Logger::close() { file_.close(); }

void Logger::write(const char* level, const std::string& message) {
    const std::string line = "[" + std::string(level) + "] " + message + "\n";
    std::cerr << line;
    if (file_) {
        file_ << line;
        file_.flush();
    }
}

std::string FormatWin32ErrorMessage(DWORD errorCode) {
    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags,
                               nullptr,
                               errorCode,
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               reinterpret_cast<LPWSTR>(&buffer),
                               0,
                               nullptr);
    std::string message = "code=" + std::to_string(errorCode);
    if (len > 0 && buffer) {
        std::wstring text(buffer, len);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
            text.pop_back();
        }
        if (!text.empty()) {
            message += " message=" + WideToUtf8(text);
        }
    }
    if (buffer) {
        LocalFree(buffer);
    }
    return message;
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

        if (arg == L"--uninstall") {
            options.uninstallMode = true;
        } else if (arg == L"--install-dir") {
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

    if (options.statePath.empty()) {
        error = "Required arguments: --state-path";
        return std::nullopt;
    }

    if (!options.uninstallMode &&
        (options.installDir.empty() || options.appVersion.empty() ||
         options.configUrl.empty())) {
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

bool HttpDownloadToSink(const std::string& url,
                        const std::function<bool(const uint8_t*, size_t)>& onChunk,
                        std::string& error) {
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

    for (int attempt = 1; attempt <= kHttpMaxAttempts; ++attempt) {
        HINTERNET session = WinHttpOpen(L"post_setup_agent/1.0",
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS,
                                        0);
        if (!session) {
            error = "WinHTTP open failed for url=" + url + " " +
                    FormatWin32ErrorMessage(GetLastError());
        } else {
            WinHttpSetTimeouts(session,
                               kHttpResolveTimeoutMs,
                               kHttpConnectTimeoutMs,
                               kHttpSendTimeoutMs,
                               kHttpReceiveTimeoutMs);

            HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
            HINTERNET request = nullptr;
            bool success = false;
            DWORD lastError = ERROR_SUCCESS;
            DWORD statusCode = 0;

            if (!connection) {
                lastError = GetLastError();
                error = "WinHTTP connect failed for url=" + url + " " +
                        FormatWin32ErrorMessage(lastError);
            } else {
                DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
                request = WinHttpOpenRequest(connection,
                                             L"GET",
                                             path.c_str(),
                                             nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             flags);
                if (!request) {
                    lastError = GetLastError();
                    error = "WinHTTP request creation failed for url=" + url + " " +
                            FormatWin32ErrorMessage(lastError);
                } else if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                    lastError = GetLastError();
                    error = "WinHTTP send failed for url=" + url + " " +
                            FormatWin32ErrorMessage(lastError);
                } else if (!WinHttpReceiveResponse(request, nullptr)) {
                    lastError = GetLastError();
                    error = "WinHTTP receive failed for url=" + url + " " +
                            FormatWin32ErrorMessage(lastError);
                } else {
                    DWORD statusSize = sizeof(statusCode);
                    if (!WinHttpQueryHeaders(request,
                                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                             WINHTTP_HEADER_NAME_BY_INDEX,
                                             &statusCode,
                                             &statusSize,
                                             WINHTTP_NO_HEADER_INDEX)) {
                        lastError = GetLastError();
                        error = "WinHTTP query status failed for url=" + url + " " +
                                FormatWin32ErrorMessage(lastError);
                    } else if (statusCode != 200) {
                        error = "HTTP request returned status " + std::to_string(statusCode) +
                                " for url=" + url;
                    } else {
                        success = true;
                        std::vector<uint8_t> buffer(64 * 1024);
                        for (;;) {
                            DWORD available = 0;
                            if (!WinHttpQueryDataAvailable(request, &available)) {
                                lastError = GetLastError();
                                error = "WinHTTP query data failed for url=" + url + " " +
                                        FormatWin32ErrorMessage(lastError);
                                success = false;
                                break;
                            }
                            if (available == 0) {
                                break;
                            }
                            const DWORD toRead =
                                (std::min)(available, static_cast<DWORD>(buffer.size()));
                            DWORD read = 0;
                            if (!WinHttpReadData(request, buffer.data(), toRead, &read)) {
                                lastError = GetLastError();
                                error = "WinHTTP read failed for url=" + url + " " +
                                        FormatWin32ErrorMessage(lastError);
                                success = false;
                                break;
                            }
                            if (read == 0) {
                                break;
                            }
                            if (!onChunk(buffer.data(), read)) {
                                error = "Failed to persist HTTP response chunk for url=" + url;
                                success = false;
                                break;
                            }
                        }
                    }
                }
            }

            if (request) WinHttpCloseHandle(request);
            if (connection) WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);

            if (success) {
                return true;
            }
        }

        if (attempt < kHttpMaxAttempts) {
            error = "attempt=" + std::to_string(attempt) + "/" +
                    std::to_string(kHttpMaxAttempts) + " " + error;
            std::this_thread::sleep_for(std::chrono::milliseconds(kHttpRetryDelayMs * attempt));
        } else {
            error = "attempt=" + std::to_string(attempt) + "/" +
                    std::to_string(kHttpMaxAttempts) + " " + error;
        }
    }

    return false;
}

bool HttpGetBytes(const std::string& url, std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    const bool ok = HttpDownloadToSink(
        url,
        [&out](const uint8_t* chunk, size_t size) {
            out.insert(out.end(), chunk, chunk + size);
            return true;
        },
        error);
    return ok;
}

bool FetchUrlToString(const std::string& url, std::string& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!HttpGetBytes(url, bytes, error)) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool FetchComponentToPath(const std::string& url,
                          const fs::path& savePath,
                          const ExpansionContext& context,
                          std::string& error) {
    if (!EnsureParentDirectory(savePath, error)) {
        return false;
    }
    if (StartsWithNoCase(url, "file://")) {
        const std::string expandedUrl = ExpandTokens(url, context);
        fs::path source = PathFromUtf8(FileUrlToPath(expandedUrl));
        std::error_code ec;
        fs::copy_file(source, savePath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Failed to copy local payload: " + Utf8FromPath(source);
            return false;
        }
        return true;
    }

    std::ofstream out(savePath, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Failed to open download target: " + Utf8FromPath(savePath);
        return false;
    }
    const bool ok = HttpDownloadToSink(
        url,
        [&out](const uint8_t* chunk, size_t size) {
            out.write(reinterpret_cast<const char*>(chunk), static_cast<std::streamsize>(size));
            return static_cast<bool>(out);
        },
        error);
    out.flush();
    if (!ok || !out) {
        std::error_code removeEc;
        out.close();
        fs::remove(savePath, removeEc);
        return false;
    }
    return true;
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

bool DeleteRegistryValueIfPresent(HKEY root,
                                  const std::wstring& subKey,
                                  const std::wstring& valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return true;
    }
    const LONG status = RegDeleteValueW(key, valueName.c_str());
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
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

std::vector<wchar_t> BuildCurrentEnvironmentBlock(
    const std::vector<std::pair<std::wstring, std::wstring>>& extraEntries) {
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> merged;

    auto normalizeKey = [](std::wstring key) {
        std::transform(key.begin(), key.end(), key.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
        return key;
    };

    LPWCH existingEnv = GetEnvironmentStringsW();
    if (existingEnv) {
        for (LPWCH current = existingEnv; *current; ) {
            std::wstring entry = current;
            size_t separator = entry.find(L'=');
            if (separator != std::wstring::npos && separator > 0) {
                std::wstring key = entry.substr(0, separator);
                std::wstring value = entry.substr(separator + 1);
                merged[normalizeKey(key)] = { key, value };
            }
            current += entry.size() + 1;
        }
        FreeEnvironmentStringsW(existingEnv);
    }

    for (const auto& entry : extraEntries) {
        if (entry.first.empty()) {
            continue;
        }
        merged[normalizeKey(entry.first)] = entry;
    }

    std::vector<wchar_t> envBlock;
    for (const auto& item : merged) {
        const std::wstring& key = item.second.first;
        const std::wstring& value = item.second.second;
        envBlock.insert(envBlock.end(), key.begin(), key.end());
        envBlock.push_back(L'=');
        envBlock.insert(envBlock.end(), value.begin(), value.end());
        envBlock.push_back(L'\0');
    }
    envBlock.push_back(L'\0');
    return envBlock;
}

bool ApplyEnvironmentEntries(const std::vector<EnvironmentEntry>& entries,
                             const ExpansionContext& context,
                             std::vector<EnvironmentEntry>& applied,
                             std::string& error) {
    PreparedEnvironmentChanges prepared;
    if (!PrepareEnvironmentChanges(entries, context, prepared, error)) {
        return false;
    }

    const std::wstring envKey = L"Environment";
    for (const auto& variable : prepared.variables) {
        if (!WriteRegistryString(HKEY_CURRENT_USER, envKey, variable.first, variable.second)) {
            error = "Failed to write environment variable: " + WideToUtf8(variable.first);
            return false;
        }
    }

    if (!WriteRegistryString(HKEY_CURRENT_USER, envKey, L"Path", prepared.joinedPath)) {
        error = "Failed to update system PATH.";
        return false;
    }

    for (const auto& variable : prepared.variables) {
        SetEnvironmentVariableW(variable.first.c_str(), variable.second.c_str());
    }
    SetEnvironmentVariableW(L"Path", prepared.joinedPath.c_str());

    SendMessageTimeoutW(HWND_BROADCAST,
                        WM_SETTINGCHANGE,
                        0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG,
                        5000,
                        nullptr);
    applied = std::move(prepared.appliedEntries);
    return true;
}

bool RollbackEnvironmentEntries(const std::vector<EnvironmentEntry>& entries,
                                std::string& error) {
    const std::wstring envKey = L"Environment";
    bool touchesPath = false;
    std::unordered_set<std::string> pathEntriesToRemove;
    for (const auto& entry : entries) {
        if (ToLowerAscii(entry.key) == "path") {
            touchesPath = true;
            const std::string normalized = NormalizePathForCompare(entry.value);
            if (!normalized.empty()) {
                pathEntriesToRemove.insert(normalized);
            }
        }
    }

    std::wstring currentPath;
    if (touchesPath && !ReadRegistryString(HKEY_CURRENT_USER, envKey, L"Path", currentPath)) {
        error = "Failed to read current user PATH from HKCU\\Environment during rollback.";
        return false;
    }

    for (const auto& entry : entries) {
        if (ToLowerAscii(entry.key) == "path") {
            continue;
        }
        const std::wstring keyW = Utf8ToWide(entry.key);
        if (!DeleteRegistryValueIfPresent(HKEY_CURRENT_USER, envKey, keyW)) {
            error = "Failed to remove environment variable: " + entry.key;
            return false;
        }
    }

    if (touchesPath) {
        const std::vector<std::wstring> pathParts = SplitPathList(currentPath);
        std::vector<std::wstring> filteredParts;
        filteredParts.reserve(pathParts.size());
        for (const auto& part : pathParts) {
            const std::string normalized = NormalizePathForCompare(WideToUtf8(part));
            if (pathEntriesToRemove.find(normalized) == pathEntriesToRemove.end()) {
                filteredParts.push_back(part);
            }
        }
        const std::wstring joinedPath = JoinPathList(filteredParts);
        if (!WriteRegistryString(HKEY_CURRENT_USER, envKey, L"Path", joinedPath)) {
            error = "Failed to update user PATH during rollback.";
            return false;
        }
        SetEnvironmentVariableW(L"Path", joinedPath.c_str());
    }

    for (const auto& entry : entries) {
        if (ToLowerAscii(entry.key) == "path") {
            continue;
        }
        const std::wstring keyW = Utf8ToWide(entry.key);
        SetEnvironmentVariableW(keyW.c_str(), nullptr);
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

bool PrepareEnvironmentChanges(const std::vector<EnvironmentEntry>& entries,
                               const ExpansionContext& context,
                               PreparedEnvironmentChanges& prepared,
                               std::string& error) {
    const std::wstring envKey = L"Environment";
    std::wstring currentPath;
    if (!ReadRegistryString(HKEY_CURRENT_USER, envKey, L"Path", currentPath)) {
        error = "Failed to read current user PATH from HKCU\\Environment.";
        return false;
    }

    std::vector<std::wstring> pathParts = SplitPathList(currentPath);
    std::unordered_set<std::string> seen;
    for (const auto& part : pathParts) {
        seen.insert(NormalizePathForCompare(WideToUtf8(part)));
    }

    prepared.variables.clear();
    prepared.appliedEntries.clear();

    for (const auto& entry : entries) {
        std::string expandedValue = ExpandTokens(entry.value, context);
        if (expandedValue.empty()) {
            continue;
        }

        EnvironmentEntry appliedEntry = entry;
        appliedEntry.value = expandedValue;

        if (ToLowerAscii(entry.key) == "path") {
            const std::string normalized = NormalizePathForCompare(expandedValue);
            if (seen.insert(normalized).second) {
                std::wstring valueW = Utf8ToWide(expandedValue);
                if (entry.order == 0) {
                    pathParts.insert(pathParts.begin(), valueW);
                } else {
                    pathParts.push_back(valueW);
                }
            }
        } else {
            prepared.variables.emplace_back(Utf8ToWide(entry.key), Utf8ToWide(expandedValue));
        }

        prepared.appliedEntries.push_back(std::move(appliedEntry));
    }

    prepared.joinedPath = JoinPathList(pathParts);
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
    std::vector<wchar_t> env = BuildCurrentEnvironmentBlock(
        { { L"POST_SETUP_COMPONENT_INSTALL_DIR", installDirW } });

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
        error = "Failed to start installer: " + Utf8FromPath(executablePath) + " " +
                FormatWin32ErrorMessage(GetLastError());
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
        if (waitResult == WAIT_TIMEOUT) {
            error = "Installer timed out after " + std::to_string(timeoutSec) +
                    " seconds.";
        } else {
            error = "Failed while waiting for installer: " +
                    FormatWin32ErrorMessage(GetLastError());
        }
        return false;
    }
    if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        CloseHandle(processInfo.hProcess);
        error = "Failed to read installer exit code: " +
                FormatWin32ErrorMessage(GetLastError());
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
    root["statePath"] = options.statePath;
    root["logPath"] = options.logPath;
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

bool ReadStateFile(const std::string& statePath,
                   PersistedState& out,
                   bool& missing,
                   std::string& error) {
    out = PersistedState{};
    missing = false;

    const fs::path path = PathFromUtf8(statePath);
    if (!fs::exists(path)) {
        missing = true;
        return true;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Failed to open state file: " + statePath;
        return false;
    }

    const std::string payload((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    if (payload.empty()) {
        error = "State file is empty: " + statePath;
        return false;
    }

    json root = json::parse(payload, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "State file is invalid JSON: " + statePath;
        return false;
    }
    if (!root.contains("environment") || !root["environment"].is_array()) {
        error = "State file missing 'environment' array: " + statePath;
        return false;
    }

    out.statePath = root.value("statePath", statePath);
    out.logPath = root.value("logPath", "");
    for (const auto& item : root["environment"]) {
        if (!item.is_object()) {
            error = "State file contains invalid environment entry.";
            return false;
        }
        EnvironmentEntry entry;
        entry.key = item.value("key", "");
        entry.value = item.value("value", "");
        entry.order = item.value("order", 0);
        if (entry.key.empty()) {
            error = "State file contains environment entry with empty key.";
            return false;
        }
        out.environment.push_back(std::move(entry));
    }
    return true;
}

bool DeleteFileIfExists(const fs::path& path, std::string& error) {
    if (path.empty()) {
        return true;
    }
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return true;
    }
    if (!fs::remove(path, ec) && ec) {
        error = "Failed to delete file: " + Utf8FromPath(path);
        return false;
    }
    return true;
}

bool RunPostSetupAgentInstall(const AgentOptions& options, Logger& logger) {
    logger.info("post_setup_agent started.");

    ExpansionContext context;
    context.installDir = options.installDir;
    context.appVersion = options.appVersion;

    std::string payload;
    std::string error;
    if (!FetchUrlToString(options.configUrl, payload, error)) {
        logger.error("[ConfigFetch] " + error);
        return false;
    }

    RemoteConfig config;
    if (!ParseRemoteConfig(payload, config, error)) {
        logger.error("[ConfigParse] " + error);
        return false;
    }

    std::string profileName;
    const RemoteProfile& profile = SelectProfile(config, options.appVersion, profileName);
    logger.info("Selected profile: " + profileName);

    std::vector<EnvironmentEntry> appliedEnvironment;
    if (!ApplyEnvironmentEntries(profile.environment, context, appliedEnvironment, error)) {
        logger.error("[Environment] " + error);
        return false;
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
            logger.error("[ComponentDownload] id=" + component.id +
                         " url=" + component.url +
                         " saveAs=" + Utf8FromPath(savePath) +
                         " installDir=" + context.componentInstallDir +
                         " error=" + error);
            return false;
        }

        std::string actualHash;
        if (!ComputeFileSha256(savePath, actualHash, error)) {
            logger.error("[ComponentHash] id=" + component.id +
                         " saveAs=" + Utf8FromPath(savePath) +
                         " error=" + error);
            return false;
        }
        if (actualHash != component.sha256) {
            logger.error("[ComponentHash] id=" + component.id +
                         " saveAs=" + Utf8FromPath(savePath) +
                         " expected=" + component.sha256 +
                         " actual=" + actualHash +
                         " error=SHA256 mismatch");
            return false;
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
            logger.error("[ComponentExecute] id=" + component.id +
                         " saveAs=" + Utf8FromPath(savePath) +
                         " installDir=" + context.componentInstallDir +
                         " wait=" + std::string(component.wait ? "true" : "false") +
                         " timeoutSec=" + std::to_string(component.timeoutSec) +
                         " error=" + error);
            return false;
        }
        if (component.wait && exitCode != 0) {
            logger.error("[ComponentExecute] id=" + component.id +
                         " saveAs=" + Utf8FromPath(savePath) +
                         " installDir=" + context.componentInstallDir +
                         " wait=true timeoutSec=" + std::to_string(component.timeoutSec) +
                         " exitCode=" + std::to_string(exitCode));
            return false;
        }
        installedComponents[component.id] = actualHash;
    }

    if (!WriteStateFile(options,
                        config.version,
                        profileName,
                        appliedEnvironment,
                        installedComponents,
                        error)) {
        logger.error("[StateWrite] " + error);
        return false;
    }

    logger.info("post_setup_agent completed successfully.");
    return true;
}

bool RunPostSetupAgentUninstall(const AgentOptions& options, Logger& logger) {
    logger.info("post_setup_agent uninstall started.");

    PersistedState state;
    bool missing = false;
    std::string error;
    if (!ReadStateFile(options.statePath, state, missing, error)) {
        logger.error("[PostSetup][Uninstall][State] " + error);
        return false;
    }
    if (missing) {
        logger.info("[PostSetup][Uninstall][State] state file not found; nothing to rollback.");
        return true;
    }

    if (!RollbackEnvironmentEntries(state.environment, error)) {
        logger.error("[PostSetup][Uninstall][Env] " + error);
        return false;
    }
    logger.info("[PostSetup][Uninstall][Env] rolled back environment entries successfully.");

    const fs::path persistedStatePath =
        PathFromUtf8(state.statePath.empty() ? options.statePath : state.statePath);
    if (!DeleteFileIfExists(persistedStatePath, error)) {
        logger.error("[PostSetup][Uninstall][Cleanup] " + error);
        return false;
    }
    logger.info("[PostSetup][Uninstall][Cleanup] removed state file: " +
                Utf8FromPath(persistedStatePath));

    const fs::path persistedLogPath =
        PathFromUtf8(!options.logPath.empty() ? options.logPath : state.logPath);
    logger.info("[PostSetup][Uninstall][Cleanup] rollback completed.");
    logger.close();

    std::string logDeleteError;
    if (!DeleteFileIfExists(persistedLogPath, logDeleteError)) {
        std::cerr << "[WARN] [PostSetup][Uninstall][Cleanup] " << logDeleteError << "\n";
    }

    return true;
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
    return (options.uninstallMode ? RunPostSetupAgentUninstall(options, logger)
                                  : RunPostSetupAgentInstall(options, logger))
               ? 0
               : 1;
}

} // namespace MultiThreadedInstaller

int wmain(int argc, wchar_t** argv) {
    return MultiThreadedInstaller::RunPostSetupAgentMain(argc, argv);
}
