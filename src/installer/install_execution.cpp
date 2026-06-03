#include "installer/install_execution.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/component_launcher.h"
#include "installer/folder_payload_reader.h"
#include "installer/install_manifest_store.h"
#include "installer/installer_helpers.h"
#include "installer/metadata_parser.h"

#include <memory>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <cwchar>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#endif

namespace MultiThreadedInstaller {

namespace {

float Clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

std::string ResolveComponentDisplayName(const ComponentConfig& component) {
    return component.name.empty() ? component.id : component.name;
}

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
}

using SteadyTimePoint = std::chrono::steady_clock::time_point;

uint64_t ElapsedMilliseconds(SteadyTimePoint start) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
}

const char* ComponentSourceTypeName(ComponentSourceType type) {
    switch (type) {
        case ComponentSourceType::LOCAL:
            return "local";
        case ComponentSourceType::DOWNLOAD:
            return "download";
        default:
            return "none";
    }
}

ComponentInstallTiming BuildComponentInstallTiming(const ComponentConfig& component,
                                                   const std::string& displayName,
                                                   bool success,
                                                   uint64_t totalMs,
                                                   uint64_t downloadMs,
                                                   uint64_t installMs,
                                                   const std::string& error) {
    ComponentInstallTiming timing;
    timing.id = component.id;
    timing.name = displayName;
    timing.type = ComponentSourceTypeName(component.source.type);
    timing.success = success;
    timing.totalMs = totalMs;
    timing.downloadMs = downloadMs;
    timing.installMs = installMs;
    timing.error = error;
    return timing;
}

std::string ExpandInstallDirToken(const std::string& text, const std::string& installDir) {
    if (text.empty()) {
        return text;
    }
    const std::string token = "%InstallDir%";
    std::string expanded = text;
    size_t position = 0;
    while ((position = expanded.find(token, position)) != std::string::npos) {
        expanded.replace(position, token.size(), installDir);
        position += installDir.size();
    }
    return expanded;
}

std::string ExpandRuntimeTokens(const std::string& text,
                                const std::string& installDir,
                                const ExtendedInstallationMetadata& metadata,
                                InstallerPathResolver& resolver,
                                const std::string& componentInstallDir = {}) {
    if (text.empty()) {
        return text;
    }

    std::string expanded = ExpandInstallDirToken(text, installDir);
    const std::vector<std::pair<std::string, std::string>> tokens = {
        { "%AppVersion%", metadata.appVersion },
        { "%AppId%", resolveEffectiveAppId(metadata.appId, metadata.appName) },
        { "%appDirectoryName%", metadata.appDirectoryName },
        { "%ComponentInstallDir%", componentInstallDir }
    };
    for (const auto& token : tokens) {
        if (token.second.empty()) {
            continue;
        }
        size_t position = 0;
        while ((position = expanded.find(token.first, position)) != std::string::npos) {
            expanded.replace(position, token.first.size(), token.second);
            position += token.second.size();
        }
    }
    return resolver.expandEnvironmentVariables(expanded);
}

void RecordComponentUninstallAction(const ComponentConfig& component,
                                    const std::string& sourceType,
                                    const std::string& defaultWorkingDirectory,
                                    const std::string& installRootForComponents,
                                    const ExtendedInstallationMetadata& metadata,
                                    InstallerPathResolver& pathResolver,
                                    const std::string& componentInstallDir,
                                    std::vector<ComponentExecutionRecord>& componentActions) {
    if (component.uninstall.command.empty()) {
        return;
    }

    const std::string expandedCommand =
        ExpandRuntimeTokens(component.uninstall.command,
                            installRootForComponents,
                            metadata,
                            pathResolver,
                            componentInstallDir);
    const std::string expandedArgs =
        ExpandRuntimeTokens(component.uninstall.args,
                            installRootForComponents,
                            metadata,
                            pathResolver,
                            componentInstallDir);
    const std::string configuredWorkingDirectory =
        component.uninstall.workingDirectory.empty() ? defaultWorkingDirectory
                                                    : component.uninstall.workingDirectory;
    ComponentExecutionRecord record;
    record.componentId = component.id;
    record.sourceType = sourceType;
    record.uninstallCommand = expandedCommand;
    if (!expandedArgs.empty()) {
        record.uninstallCommand += " " + expandedArgs;
    }
    record.workingDirectory =
        ExpandRuntimeTokens(configuredWorkingDirectory,
                            installRootForComponents,
                            metadata,
                            pathResolver,
                            componentInstallDir);
    record.wait = component.uninstall.wait;
    record.timeoutSec = component.uninstall.timeoutSec;
    componentActions.push_back(std::move(record));
}

std::string NormalizePathString(const std::filesystem::path& path) {
    std::string value = Utf8FromPath(path.lexically_normal());
    std::replace(value.begin(), value.end(), '/', '\\');
    while (!value.empty() && (value.back() == '\\' || value.back() == '/')) {
        value.pop_back();
    }
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsPathUnderBase(const std::filesystem::path& base, const std::filesystem::path& candidate) {
    const std::string baseNormalized = NormalizePathString(base);
    const std::string candidateNormalized = NormalizePathString(candidate);
    if (baseNormalized.empty() || candidateNormalized.empty()) {
        return false;
    }
    if (candidateNormalized == baseNormalized) {
        return true;
    }
    if (candidateNormalized.size() <= baseNormalized.size()) {
        return false;
    }
    if (candidateNormalized.compare(0, baseNormalized.size(), baseNormalized) != 0) {
        return false;
    }
    return candidateNormalized[baseNormalized.size()] == '\\';
}

#ifdef _WIN32
HANDLE CreateComponentJobObject() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        logInstallerWarning("[ComponentInstall] Failed to create job object for component process tree.");
        return nullptr;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job,
                                 JobObjectExtendedLimitInformation,
                                 &limits,
                                 sizeof(limits))) {
        logInstallerWarning("[ComponentInstall] Failed to configure component job object.");
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

bool AssignComponentProcessToJob(HANDLE jobHandle, HANDLE processHandle) {
    if (!jobHandle || !processHandle) {
        return false;
    }
    if (AssignProcessToJobObject(jobHandle, processHandle)) {
        return true;
    }
    logInstallerWarning("[ComponentInstall] Failed to assign component process to job object; "
                        "falling back to single-process termination on timeout/cancel.");
    return false;
}

void TerminateComponentProcessTree(HANDLE jobHandle, HANDLE processHandle) {
    if (jobHandle && TerminateJobObject(jobHandle, 1)) {
        return;
    }
    if (jobHandle) {
        logInstallerWarning("[ComponentInstall] Failed to terminate component job object; "
                            "falling back to launcher process termination.");
    }
    if (processHandle) {
        TerminateProcess(processHandle, 1);
    }
}

bool WaitForProcessExit(HANDLE processHandle,
                        HANDLE jobHandle,
                        uint32_t timeoutSec,
                        const std::function<bool()>& cancellationCallback,
                        const std::function<void(uint64_t, uint32_t)>& heartbeatCallback,
                        DWORD& exitCode,
                        std::string& error) {
    const uint64_t timeoutMs = timeoutSec == 0
                                   ? std::numeric_limits<uint64_t>::max()
                                   : static_cast<uint64_t>(timeoutSec) * 1000ULL;
    uint64_t waitedMs = 0;
    uint64_t lastHeartbeatMs = 0;

    while (true) {
        if (cancellationCallback && cancellationCallback()) {
            TerminateComponentProcessTree(jobHandle, processHandle);
            WaitForSingleObject(processHandle, 2000);
            error = "Component execution cancelled.";
            return false;
        }

        DWORD sliceMs = 200;
        if (timeoutMs != std::numeric_limits<uint64_t>::max()) {
            if (waitedMs >= timeoutMs) {
                TerminateComponentProcessTree(jobHandle, processHandle);
                WaitForSingleObject(processHandle, 2000);
                error = "Component execution timed out.";
                return false;
            }
            const uint64_t remaining = timeoutMs - waitedMs;
            if (remaining < sliceMs) {
                sliceMs = static_cast<DWORD>(remaining);
            }
        }

        DWORD waitResult = WaitForSingleObject(processHandle, sliceMs);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult != WAIT_TIMEOUT) {
            error = "Failed while waiting for component process.";
            return false;
        }
        waitedMs += sliceMs;
        if (heartbeatCallback && waitedMs - lastHeartbeatMs >= 1000ULL) {
            lastHeartbeatMs = waitedMs;
            heartbeatCallback(waitedMs / 1000ULL, timeoutSec);
        }
    }

    if (!GetExitCodeProcess(processHandle, &exitCode)) {
        error = "Failed to read component process exit code.";
        return false;
    }
    return true;
}

bool ExecuteProcess(const std::filesystem::path& executablePath,
                    const std::string& args,
                    bool showWindow,
                    bool showWindowConfigured,
                    bool wait,
                    uint32_t timeoutSec,
                    const std::function<bool()>& cancellationCallback,
                    const std::function<void(uint64_t, uint32_t)>& heartbeatCallback,
                    DWORD& exitCode,
                    std::string& error) {
    std::wstring executableW = executablePath.wstring();
    if (executableW.empty()) {
        error = "Component executable path is empty.";
        return false;
    }

    const ComponentLaunchCommand launchCommand =
        BuildComponentLaunchCommand(executablePath, args);

    std::vector<wchar_t> commandLineBuffer(launchCommand.commandLine.begin(),
                                           launchCommand.commandLine.end());
    commandLineBuffer.push_back(L'\0');

    std::wstring workingDirectory;
    if (executablePath.has_parent_path()) {
        workingDirectory = executablePath.parent_path().wstring();
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    DWORD creationFlags = 0;
    if (showWindowConfigured) {
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = showWindow ? SW_SHOWNORMAL : SW_HIDE;
        if (!showWindow) {
            creationFlags = CREATE_NO_WINDOW;
        }
    } else if (launchCommand.hideByDefault) {
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;
        creationFlags = CREATE_NO_WINDOW;
    }
    PROCESS_INFORMATION processInfo{};
    HANDLE jobHandle = wait ? CreateComponentJobObject() : nullptr;

    BOOL started = CreateProcessW(nullptr,
                                  commandLineBuffer.data(),
                                  nullptr,
                                  nullptr,
                                  FALSE,
                                  creationFlags,
                                  nullptr,
                                  workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                  &startupInfo,
                                  &processInfo);
    if (!started) {
        if (jobHandle) {
            CloseHandle(jobHandle);
        }
        error = launchCommand.startFailureMessage;
        return false;
    }

    const bool assignedToJob = AssignComponentProcessToJob(jobHandle, processInfo.hProcess);
    if (jobHandle && !assignedToJob) {
        CloseHandle(jobHandle);
        jobHandle = nullptr;
    }

    CloseHandle(processInfo.hThread);
    processInfo.hThread = nullptr;

    if (!wait) {
        exitCode = 0;
        CloseHandle(processInfo.hProcess);
        return true;
    }

    bool ok = WaitForProcessExit(processInfo.hProcess,
                                 jobHandle,
                                 timeoutSec,
                                 cancellationCallback,
                                 heartbeatCallback,
                                 exitCode,
                                 error);
    CloseHandle(processInfo.hProcess);
    if (jobHandle) {
        CloseHandle(jobHandle);
    }
    return ok;
}

std::string ToLowerAscii(std::string value);

class StreamingSha256 {
public:
    bool Initialize(std::string& error) {
        DWORD bytesRead = 0;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status < 0) {
            error = "Failed to open SHA256 provider.";
            return false;
        }
        status = BCryptGetProperty(algorithm_,
                                   BCRYPT_OBJECT_LENGTH,
                                   reinterpret_cast<PUCHAR>(&objectLength_),
                                   sizeof(objectLength_),
                                   &bytesRead,
                                   0);
        if (status < 0) {
            error = "Failed to read SHA256 object length.";
            Cleanup();
            return false;
        }
        status = BCryptGetProperty(algorithm_,
                                   BCRYPT_HASH_LENGTH,
                                   reinterpret_cast<PUCHAR>(&hashLength_),
                                   sizeof(hashLength_),
                                   &bytesRead,
                                   0);
        if (status < 0 || hashLength_ == 0) {
            error = "Failed to read SHA256 hash length.";
            Cleanup();
            return false;
        }
        hashObject_.resize(objectLength_);
        status = BCryptCreateHash(algorithm_,
                                  &hashHandle_,
                                  hashObject_.data(),
                                  objectLength_,
                                  nullptr,
                                  0,
                                  0);
        if (status < 0) {
            error = "Failed to create SHA256 hash.";
            Cleanup();
            return false;
        }
        return true;
    }

    bool Update(const void* data, size_t size, std::string& error) {
        if (!hashHandle_ || size == 0) {
            return true;
        }
        if (size > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
            error = "SHA256 chunk is too large.";
            return false;
        }
        NTSTATUS status = BCryptHashData(hashHandle_,
                                         reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
                                         static_cast<ULONG>(size),
                                         0);
        if (status < 0) {
            error = "Failed to hash downloaded component installer.";
            return false;
        }
        return true;
    }

    bool Finish(std::string& sha256, std::string& error) {
        if (!hashHandle_) {
            sha256.clear();
            return true;
        }
        std::vector<unsigned char> hash(hashLength_);
        NTSTATUS status = BCryptFinishHash(hashHandle_, hash.data(), hashLength_, 0);
        if (status < 0) {
            error = "Failed to finish SHA256 hash.";
            return false;
        }
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (unsigned char byte : hash) {
            oss << std::setw(2) << static_cast<unsigned int>(byte);
        }
        sha256 = oss.str();
        return true;
    }

    ~StreamingSha256() {
        Cleanup();
    }

private:
    void Cleanup() {
        if (hashHandle_) {
            BCryptDestroyHash(hashHandle_);
            hashHandle_ = nullptr;
        }
        if (algorithm_) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
            algorithm_ = nullptr;
        }
    }

    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hashHandle_ = nullptr;
    DWORD objectLength_ = 0;
    DWORD hashLength_ = 0;
    std::vector<unsigned char> hashObject_;
};

struct DownloadProgressState {
    uint64_t bytesDownloaded = 0;
    uint64_t contentLength = 0;
};

std::string FormatBytes(uint64_t bytes) {
    static const char* units[] = { "B", "KB", "MB", "GB" };
    constexpr size_t unitCount = sizeof(units) / sizeof(units[0]);
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < unitCount) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream oss;
    if (unit == 0) {
        oss << bytes << " " << units[unit];
    } else {
        oss << std::fixed << std::setprecision(1) << value << " " << units[unit];
    }
    return oss.str();
}

std::filesystem::path MakeDownloadTempPath(const std::filesystem::path& targetPath) {
    std::wstring tempName = targetPath.filename().wstring();
    tempName += L".mti_download_";
    tempName += std::to_wstring(GetCurrentProcessId());
    tempName += L".tmp";
    return targetPath.parent_path() / tempName;
}

bool QueryContentLength(HINTERNET request, uint64_t& contentLength) {
    contentLength = 0;
    wchar_t buffer[64] = {};
    DWORD bufferSize = sizeof(buffer);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_CONTENT_LENGTH,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             buffer,
                             &bufferSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        return false;
    }
    wchar_t* end = nullptr;
    unsigned long long parsed = std::wcstoull(buffer, &end, 10);
    if (end == buffer) {
        return false;
    }
    contentLength = static_cast<uint64_t>(parsed);
    return true;
}

bool DownloadFileToPathStreaming(
    const std::string& url,
    const std::filesystem::path& targetPath,
    const std::string& expectedSha256,
    const std::function<bool()>& cancellationCallback,
    const std::function<void(const DownloadProgressState&)>& progressCallback,
    std::string& actualSha256,
    std::string& error) {
    const std::wstring urlW = Utf8ToWide(url);
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(urlW.c_str(), 0, 0, &parts)) {
        error = "Invalid component download URL.";
        return false;
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTPS) {
        error = "Component download URL must use HTTPS.";
        return false;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }

    HINTERNET session = WinHttpOpen(L"MTInstaller/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) {
        error = "Failed to initialize WinHTTP session.";
        return false;
    }
    auto closeSession = [&]() {
        if (session) {
            WinHttpCloseHandle(session);
            session = nullptr;
        }
    };
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 30000);

    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    if (!connection) {
        closeSession();
        error = "Failed to connect to component download host.";
        return false;
    }
    auto closeConnection = [&]() {
        if (connection) {
            WinHttpCloseHandle(connection);
            connection = nullptr;
        }
    };

    HINTERNET request = WinHttpOpenRequest(connection,
                                           L"GET",
                                           path.c_str(),
                                           nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        closeConnection();
        closeSession();
        error = "Failed to create component download request.";
        return false;
    }
    auto closeRequest = [&]() {
        if (request) {
            WinHttpCloseHandle(request);
            request = nullptr;
        }
    };

    auto fail = [&](const std::string& message) {
        closeRequest();
        closeConnection();
        closeSession();
        error = message;
        return false;
    };

    if (!WinHttpSendRequest(request,
                            WINHTTP_NO_ADDITIONAL_HEADERS,
                            0,
                            WINHTTP_NO_REQUEST_DATA,
                            0,
                            0,
                            0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        return fail("Failed to download component installer.");
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &statusCode,
                             &statusCodeSize,
                             WINHTTP_NO_HEADER_INDEX) ||
        statusCode < 200 || statusCode >= 300) {
        std::ostringstream oss;
        oss << "Component download failed with HTTP status " << statusCode << ".";
        return fail(oss.str());
    }

    DownloadProgressState progress;
    QueryContentLength(request, progress.contentLength);

    const std::filesystem::path tempPath = MakeDownloadTempPath(targetPath);
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return fail("Failed to open temporary component download file.");
    }

    StreamingSha256 sha256;
    const bool needsHash = !expectedSha256.empty();
    if (needsHash && !sha256.Initialize(error)) {
        output.close();
        std::filesystem::remove(tempPath, ec);
        closeRequest();
        closeConnection();
        closeSession();
        return false;
    }

    auto lastProgressTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(250);
    uint64_t lastProgressBytes = 0;
    auto emitProgress = [&](bool force) {
        if (!progressCallback) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const bool enoughTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressTime).count() >= 250;
        const bool enoughBytes = progress.bytesDownloaded - lastProgressBytes >= 1024ULL * 1024ULL;
        if (force || enoughTime || enoughBytes) {
            lastProgressTime = now;
            lastProgressBytes = progress.bytesDownloaded;
            progressCallback(progress);
        }
    };
    emitProgress(true);

    std::array<unsigned char, 64 * 1024> buffer{};
    while (true) {
        if (cancellationCallback && cancellationCallback()) {
            output.close();
            std::filesystem::remove(tempPath, ec);
            return fail("Component download cancelled.");
        }

        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            output.close();
            std::filesystem::remove(tempPath, ec);
            return fail("Failed while reading component download.");
        }
        if (available == 0) {
            break;
        }

        while (available > 0) {
            const DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            DWORD bytesRead = 0;
            if (!WinHttpReadData(request, buffer.data(), toRead, &bytesRead)) {
                output.close();
                std::filesystem::remove(tempPath, ec);
                return fail("Failed while reading component download.");
            }
            if (bytesRead == 0) {
                break;
            }
            output.write(reinterpret_cast<const char*>(buffer.data()), bytesRead);
            if (!output) {
                output.close();
                std::filesystem::remove(tempPath, ec);
                return fail("Failed to write component download file.");
            }
            if (needsHash && !sha256.Update(buffer.data(), bytesRead, error)) {
                output.close();
                std::filesystem::remove(tempPath, ec);
                closeRequest();
                closeConnection();
                closeSession();
                return false;
            }
            progress.bytesDownloaded += bytesRead;
            available -= bytesRead;
            emitProgress(false);
        }
    }

    output.close();
    if (!output) {
        std::filesystem::remove(tempPath, ec);
        return fail("Failed to finalize component download file.");
    }
    emitProgress(true);

    if (needsHash) {
        if (!sha256.Finish(actualSha256, error)) {
            std::filesystem::remove(tempPath, ec);
            closeRequest();
            closeConnection();
            closeSession();
            return false;
        }
        if (ToLowerAscii(actualSha256) != ToLowerAscii(expectedSha256)) {
            std::filesystem::remove(tempPath, ec);
            return fail("Downloaded component installer SHA256 mismatch.");
        }
    }

    if (!MoveFileExW(tempPath.wstring().c_str(),
                     targetPath.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        std::filesystem::remove(tempPath, ec);
        return fail("Failed to move downloaded component installer into place.");
    }

    closeRequest();
    closeConnection();
    closeSession();
    return true;
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

#endif

bool ExecuteLocalComponent(const ComponentConfig& component,
                           const std::string& installRootForComponents,
                           const ExtendedInstallationMetadata& metadata,
                           InstallerPathResolver& pathResolver,
                           const InstallServiceOptions& options,
                           std::vector<ComponentExecutionRecord>& componentActions,
                           const std::function<void(float, const std::string&)>& progressCallback,
                           uint64_t& installMs,
                           std::string& componentError) {
#ifdef _WIN32
    installMs = 0;
    const std::string baseUtf8 =
        ExpandRuntimeTokens(component.source.local.base,
                            installRootForComponents,
                            metadata,
                            pathResolver);
    std::filesystem::path basePath = PathFromUtf8(baseUtf8);
    std::filesystem::path installerPath =
        basePath / PathFromUtf8(component.source.local.installer);
    installerPath = installerPath.lexically_normal();
    const std::string componentInstallDir =
        ExpandRuntimeTokens(component.source.local.base,
                            installRootForComponents,
                            metadata,
                            pathResolver);
    const std::string expandedArgs =
        ExpandRuntimeTokens(component.source.local.args,
                            installRootForComponents,
                            metadata,
                            pathResolver,
                            componentInstallDir);

    if (!IsPathUnderBase(basePath, installerPath)) {
        componentError = "Local installer path escapes component base directory.";
    } else if (!std::filesystem::exists(installerPath)) {
        componentError = "Local installer not found: " + Utf8FromPath(installerPath);
    } else {
        DWORD exitCode = 0;
        std::string executeError;
        const auto executeStart = std::chrono::steady_clock::now();
        if (!ExecuteProcess(installerPath,
                            expandedArgs,
                            component.source.local.showWindow,
                            component.source.local.showWindowConfigured,
                            component.source.local.wait,
                            component.source.local.timeoutSec,
                            options.cancellationCallback,
                            [&](uint64_t elapsedSec, uint32_t timeoutSec) {
                                if (!progressCallback) {
                                    return;
                                }
                                float offset = 0.0f;
                                if (timeoutSec > 0) {
                                    offset = std::min(0.9f,
                                                      static_cast<float>(elapsedSec) /
                                                          static_cast<float>(timeoutSec) * 0.9f);
                                } else {
                                    offset = std::min(0.9f,
                                                      static_cast<float>(elapsedSec) /
                                                          static_cast<float>(elapsedSec + 30ULL) * 0.9f);
                                }
                                std::string message = "Installing component: " +
                                                      ResolveComponentDisplayName(component) +
                                                      " (elapsed " + std::to_string(elapsedSec) + "s";
                                if (timeoutSec > 0) {
                                    message += "/" + std::to_string(timeoutSec) + "s";
                                }
                                message += ")";
                                progressCallback(offset, message);
                            },
                            exitCode,
                            executeError)) {
            installMs = ElapsedMilliseconds(executeStart);
            componentError = executeError.empty() ? "Failed to execute local component installer."
                                                  : executeError;
        } else if (component.source.local.wait && exitCode != 0) {
            installMs = ElapsedMilliseconds(executeStart);
            componentError = "Local component installer failed with exit code " +
                             std::to_string(exitCode);
        } else {
            installMs = ElapsedMilliseconds(executeStart);
            RecordComponentUninstallAction(component,
                                           "local",
                                           Utf8FromPath(basePath),
                                           installRootForComponents,
                                           metadata,
                                           pathResolver,
                                           componentInstallDir,
                                           componentActions);
        }
    }
#else
    (void)component;
    (void)installRootForComponents;
    (void)metadata;
    (void)pathResolver;
    (void)options;
    (void)componentActions;
    (void)progressCallback;
    (void)installMs;
    componentError = "Local component installers are currently supported on Windows only.";
#endif
    return componentError.empty();
}

bool ExecuteDownloadComponent(const ComponentConfig& component,
                              const std::string& installRootForComponents,
                              const ExtendedInstallationMetadata& metadata,
                              InstallerPathResolver& pathResolver,
                              const InstallServiceOptions& options,
                              std::vector<ComponentExecutionRecord>& componentActions,
                              const std::function<void(float, const std::string&)>& progressCallback,
                              uint64_t& downloadMs,
                              uint64_t& installMs,
                              std::string& componentError) {
#ifdef _WIN32
    downloadMs = 0;
    installMs = 0;
    const std::string saveAsUtf8 =
        ExpandRuntimeTokens(component.source.download.saveAs,
                            installRootForComponents,
                            metadata,
                            pathResolver);
    std::filesystem::path installRoot = PathFromUtf8(installRootForComponents);
    std::filesystem::path targetPath = PathFromUtf8(saveAsUtf8).lexically_normal();
    if (targetPath.empty()) {
        componentError = "Downloaded component installer save path is empty.";
        return false;
    }
    if (!IsPathUnderBase(installRoot, targetPath)) {
        componentError = "Downloaded component installer save path escapes install directory.";
        return false;
    }

    std::error_code ec;
    if (targetPath.has_parent_path()) {
        std::filesystem::create_directories(targetPath.parent_path(), ec);
        if (ec) {
            componentError = "Failed to create download target directory: " + ec.message();
            return false;
        }
    }

    const std::string downloadUrl =
        ExpandRuntimeTokens(component.source.download.url,
                            installRootForComponents,
                            metadata,
                            pathResolver);
    std::string error;
    std::string actualSha256;
    auto downloadProgress = [&](const DownloadProgressState& state) {
        if (!progressCallback) {
            return;
        }
        float offset = 0.05f;
        std::string bytesText = FormatBytes(state.bytesDownloaded);
        if (state.contentLength > 0) {
            const double ratio =
                std::min(1.0, static_cast<double>(state.bytesDownloaded) /
                                  static_cast<double>(state.contentLength));
            offset = static_cast<float>(ratio * 0.6);
            bytesText += " / " + FormatBytes(state.contentLength);
        } else if (state.bytesDownloaded > 0) {
            offset = 0.1f;
        }
        progressCallback(offset,
                         "Downloading component: " + ResolveComponentDisplayName(component) +
                             " (" + bytesText + ")");
    };
    const auto downloadStart = std::chrono::steady_clock::now();
    if (!DownloadFileToPathStreaming(downloadUrl,
                                     targetPath,
                                     component.source.download.sha256,
                                     options.cancellationCallback,
                                     downloadProgress,
                                     actualSha256,
                                     error)) {
        downloadMs = ElapsedMilliseconds(downloadStart);
        componentError = error;
        return false;
    }
    downloadMs = ElapsedMilliseconds(downloadStart);

    if (!component.source.download.sha256.empty() && progressCallback) {
        progressCallback(0.6f,
                         "Verified component download: " + ResolveComponentDisplayName(component));
    }

    const std::string componentInstallDir = Utf8FromPath(targetPath.parent_path());
    const std::string expandedArgs =
        ExpandRuntimeTokens(component.source.download.args,
                            installRootForComponents,
                            metadata,
                            pathResolver,
                            componentInstallDir);

    DWORD exitCode = 0;
    const auto executeStart = std::chrono::steady_clock::now();
    if (!ExecuteProcess(targetPath,
                        expandedArgs,
                        component.source.download.showWindow,
                        component.source.download.showWindowConfigured,
                        component.source.download.wait,
                        component.source.download.timeoutSec,
                        options.cancellationCallback,
                        [&](uint64_t elapsedSec, uint32_t timeoutSec) {
                            if (!progressCallback) {
                                return;
                            }
                            float executeProgress = 0.0f;
                            if (timeoutSec > 0) {
                                executeProgress =
                                    std::min(1.0f,
                                             static_cast<float>(elapsedSec) /
                                                 static_cast<float>(timeoutSec));
                            } else {
                                executeProgress =
                                    std::min(1.0f,
                                             static_cast<float>(elapsedSec) /
                                                 static_cast<float>(elapsedSec + 30ULL));
                            }
                            const float offset = 0.6f + executeProgress * 0.35f;
                            std::string message = "Installing component: " +
                                                  ResolveComponentDisplayName(component) +
                                                  " (elapsed " + std::to_string(elapsedSec) + "s";
                            if (timeoutSec > 0) {
                                message += "/" + std::to_string(timeoutSec) + "s";
                            }
                            message += ")";
                            progressCallback(offset, message);
                        },
                        exitCode,
                        error)) {
        installMs = ElapsedMilliseconds(executeStart);
        componentError = error.empty() ? "Failed to execute downloaded component installer." : error;
        return false;
    }
    installMs = ElapsedMilliseconds(executeStart);
    if (component.source.download.wait && exitCode != 0) {
        componentError = "Downloaded component installer failed with exit code " +
                         std::to_string(exitCode);
        return false;
    }

    RecordComponentUninstallAction(component,
                                   "download",
                                   componentInstallDir,
                                   installRootForComponents,
                                   metadata,
                                   pathResolver,
                                   componentInstallDir,
                                   componentActions);
#else
    (void)component;
    (void)installRootForComponents;
    (void)metadata;
    (void)pathResolver;
    (void)options;
    (void)componentActions;
    (void)progressCallback;
    (void)downloadMs;
    (void)installMs;
    componentError = "Download component installers are currently supported on Windows only.";
#endif
    return componentError.empty();
}

} // namespace

bool ExecuteInstallExecution(const ExtendedInstallationMetadata& metadata,
                             MetadataParser& parser,
                             const InstallExecutionPlan& plan,
                             const InstallServiceOptions& options,
                             InstallerPathResolver& pathResolver,
                             InstallProgressReporter& reporter,
                             InstallExecutionOutput& output) {
    output = InstallExecutionOutput{};
    output.success = false;

    reporter.EmitStatus(InstallServiceStatus::Installing,
                        InstallServicePhase::Installing,
                        0.0f,
                        "Installing files...");

    size_t executableComponentCount = 0;
    for (const auto* component : plan.componentPlan.ordered) {
        if (!component) {
            continue;
        }
        if (component->source.type == ComponentSourceType::LOCAL ||
            component->source.type == ComponentSourceType::DOWNLOAD) {
            ++executableComponentCount;
        }
    }
    const float extractionWeight = executableComponentCount > 0 ? 0.75f : 1.0f;

    std::unordered_map<std::string, uint64_t> folderSizes;
    std::unordered_map<std::string, float> folderProgress;
    folderSizes.reserve(plan.selectedEmbeddedFolders.size());
    folderProgress.reserve(plan.selectedEmbeddedFolders.size());
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        if (std::find(plan.selectedEmbeddedFolders.begin(),
                      plan.selectedEmbeddedFolders.end(),
                      mapping.folderId) == plan.selectedEmbeddedFolders.end()) {
            continue;
        }
        folderSizes[mapping.folderName] = mapping.originalSize;
        folderProgress[mapping.folderName] = 0.0f;
    }
    std::mutex installProgressMutex;

    ProgressCallback progressCallback = [&](const std::string& folder,
                                            const std::string& currentFile,
                                            float progress) {
        float phaseProgress = Clamp01(progress);
        if (plan.totalInstallBytes > 0) {
            std::lock_guard<std::mutex> lock(installProgressMutex);
            folderProgress[folder] = Clamp01(progress);
            double completed = 0.0;
            for (const auto& entry : folderProgress) {
                auto sizeIt = folderSizes.find(entry.first);
                if (sizeIt != folderSizes.end()) {
                    completed += static_cast<double>(sizeIt->second) * static_cast<double>(entry.second);
                }
            }
            phaseProgress =
                static_cast<float>(completed / static_cast<double>(plan.totalInstallBytes));
        }
        reporter.EmitProgress(folder, currentFile, phaseProgress * extractionWeight);
    };

    LogCallback infoCallback = [&](const std::string& message) {
        reporter.EmitMessage(InstallServiceEventType::Info, message);
    };
    LogCallback errorCallback = [&](const std::string& message) {
        reporter.EmitMessage(InstallServiceEventType::Error, message);
    };

    ParallelInstallResult parallelResult;
    FolderPayloadReader payloadReader(parser.getDataPackagePath());
    if (plan.selectedEmbeddedFolders.empty() && plan.componentPlan.hasComponents) {
        parallelResult.success = true;
        parallelResult.installRootPath = plan.pathDecision.resolvedInstallRoot;
        reporter.EmitMessage(InstallServiceEventType::Info,
                             "No embedded folders selected; skipping package extraction.");
        logInstallerInfo("[InstallFlow][Extract] skipped embedded extraction");
    } else {
        logInstallerInfo(std::string("[InstallFlow][Extract] start folderCount=") +
                         std::to_string(plan.selectedEmbeddedFolders.size()) +
                         " installPath=" + options.installPath);

        // Use the previous install's per-file fingerprints (captured during
        // planning, before cleanup deleted the old manifest) so unchanged files
        // can be skipped without reading them from disk (Scheme A, zero read).
        std::shared_ptr<const InstalledFileFingerprintMap> oldInstalledFingerprints =
            plan.previousInstalledFingerprints;
        if (oldInstalledFingerprints) {
            logInstallerInfo("[InstallFlow][Extract] zero-read fingerprints available count=" +
                             std::to_string(oldInstalledFingerprints->size()));
        }

        parallelResult = RunParallelInstall(metadata,
                                            payloadReader,
                                            pathResolver,
                                            options.installPath,
                                            options.folderMappings,
                                            plan.selectedEmbeddedFolders,
                                            plan.componentPlan.hasComponents,
                                            0,
                                            progressCallback,
                                            infoCallback,
                                            errorCallback,
                                            options.cancellationCallback,
                                            oldInstalledFingerprints);
        logInstallerInfo(std::string("[InstallFlow][Extract] end success=") +
                         (parallelResult.success ? "true" : "false") +
                         " cancelled=" + (parallelResult.cancelled ? "true" : "false") +
                         " errors=" + std::to_string(parallelResult.errors.size()) +
                         " installRootPath=" + parallelResult.installRootPath);
    }

    output.timing = parallelResult.timing;
    output.installRootPath = parallelResult.installRootPath;
    output.installedRoots = std::move(parallelResult.installedRoots);
    output.installedFiles = std::move(parallelResult.installedFiles);
    output.cancelled = parallelResult.cancelled;
    output.rebootRequired = parallelResult.rebootRequired;
    output.pendingReplaceFiles = std::move(parallelResult.pendingReplaceFiles);

    if (!parallelResult.success) {
        logInstallerError("[InstallFlow][Extract] failed, aborting installation");
        output.errors = std::move(parallelResult.errors);
        if (output.cancelled && output.errors.empty()) {
            output.errors.push_back("Installation cancelled.");
        }
        if (output.errors.empty()) {
            output.errors.push_back("Installation failed.");
        }
        for (const auto& error : output.errors) {
            reporter.EmitMessage(InstallServiceEventType::Error, error);
        }
        return false;
    }

    if (output.rebootRequired) {
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Some locked files were scheduled for replacement after reboot.");
    }

    if (output.installRootPath.empty()) {
        output.installRootPath = plan.pathDecision.resolvedInstallRoot.empty()
                                     ? options.installPath
                                     : plan.pathDecision.resolvedInstallRoot;
    }

    reporter.EmitProgress("", "File installation completed", extractionWeight);

    output.componentActions.reserve(executableComponentCount);
    std::unordered_set<std::string> failedOptionalComponentIds;
    failedOptionalComponentIds.reserve(executableComponentCount);
    output.failedOptionalComponentMessages.reserve(executableComponentCount);

    if (executableComponentCount > 0) {
        const std::string installRootForComponents = output.installRootPath.empty()
                                                         ? plan.pathDecision.diskCheckPath
                                                         : output.installRootPath;
        size_t completedComponents = 0;

        auto advanceComponentProgress = [&](const std::string& displayName,
                                            float offset,
                                            const std::string& detail = {}) {
            float progress = extractionWeight +
                             ((static_cast<float>(completedComponents) + offset) /
                              static_cast<float>(executableComponentCount)) *
                                 (1.0f - extractionWeight);
            reporter.EmitProgress("component", detail.empty() ? displayName : detail, progress);
        };

        for (const auto* component : plan.componentPlan.ordered) {
            if (!component) {
                continue;
            }
            if (component->source.type != ComponentSourceType::LOCAL &&
                component->source.type != ComponentSourceType::DOWNLOAD) {
                continue;
            }

            if (IsCancellationRequested(options)) {
                output.cancelled = true;
                output.errors.push_back("Installation cancelled.");
                return false;
            }

            const std::string componentDisplayName = ResolveComponentDisplayName(*component);

            advanceComponentProgress(componentDisplayName, 0.0f);
            reporter.EmitMessage(InstallServiceEventType::Info,
                                 "Installing component: " + componentDisplayName +
                                     " (id=" + component->id + ")");

            std::string componentError;
            uint64_t downloadMs = 0;
            uint64_t installMs = 0;
            const auto componentStart = std::chrono::steady_clock::now();
            if (component->source.type == ComponentSourceType::LOCAL) {
                ExecuteLocalComponent(*component,
                                      installRootForComponents,
                                      metadata,
                                      pathResolver,
                                      options,
                                      output.componentActions,
                                      [&](float offset, const std::string& detail) {
                                          advanceComponentProgress(componentDisplayName,
                                                                   Clamp01(offset),
                                                                   detail);
                                      },
                                      installMs,
                                      componentError);
            } else {
                ExecuteDownloadComponent(*component,
                                         installRootForComponents,
                                         metadata,
                                         pathResolver,
                                         options,
                                         output.componentActions,
                                         [&](float offset, const std::string& detail) {
                                             advanceComponentProgress(componentDisplayName,
                                                                      Clamp01(offset),
                                                                      detail);
                                         },
                                         downloadMs,
                                         installMs,
                                         componentError);
            }
            const uint64_t totalMs = ElapsedMilliseconds(componentStart);
            output.componentTimings.push_back(
                BuildComponentInstallTiming(*component,
                                            componentDisplayName,
                                            componentError.empty(),
                                            totalMs,
                                            downloadMs,
                                            installMs,
                                            componentError));

            if (!componentError.empty()) {
                std::string failureMessage =
                    "Component '" + componentDisplayName + "' failed: " + componentError;
                if (component->required) {
                    output.cancelled = IsCancellationRequested(options);
                    output.errors.push_back(failureMessage);
                    reporter.EmitMessage(InstallServiceEventType::Error, failureMessage);
                    return false;
                }

                failedOptionalComponentIds.insert(component->id);
                output.failedOptionalComponentMessages.push_back(failureMessage);
                reporter.EmitMessage(InstallServiceEventType::Warning,
                                     failureMessage + " Continuing because component is optional.");
                ++completedComponents;
                advanceComponentProgress(componentDisplayName, 0.0f);
                continue;
            }

            std::string successMessage;
            if ((component->source.type == ComponentSourceType::LOCAL &&
                 !component->source.local.wait) ||
                (component->source.type == ComponentSourceType::DOWNLOAD &&
                 !component->source.download.wait)) {
                successMessage = "Component '" + componentDisplayName +
                                 "' installer launched (not waiting for completion).";
            } else {
                successMessage = "Component '" + componentDisplayName + "' installed successfully.";
            }
            reporter.EmitMessage(InstallServiceEventType::Info, successMessage);

            ++completedComponents;
            advanceComponentProgress(componentDisplayName, 0.0f);
        }
    }

    output.effectiveRegistry = plan.effectiveRegistry;
    output.effectiveAutoStartup = plan.effectiveAutoStartup;
    output.effectiveDesktopIcons = plan.effectiveDesktopIcons;
    output.effectiveKillProcesses = plan.effectiveKillProcesses;

    if (!failedOptionalComponentIds.empty()) {
        std::string message = "Optional components failed:";
        for (const auto& id : failedOptionalComponentIds) {
            message += " ";
            message += id;
        }
        reporter.EmitMessage(InstallServiceEventType::Warning, message);
    }

    output.success = true;
    return true;
}

} // namespace MultiThreadedInstaller
