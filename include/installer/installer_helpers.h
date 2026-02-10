#pragma once

#include "common/types.h"
#include "installer/path_resolver.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#else
#include <unistd.h>
#endif

namespace MultiThreadedInstaller {

std::filesystem::path toLongPath(const std::filesystem::path& path);
bool ensureFileWithSize(const std::filesystem::path& path, uint64_t size,
                        uint64_t sparseThresholdBytes = 4 * 1024 * 1024);
bool openFileForWrite(const std::filesystem::path& path, std::fstream& stream);
std::wstring toWideUtf8(const std::string& text);
std::filesystem::path findPrimaryExecutable(const std::filesystem::path& installRoot,
                                            const std::string& appName);
bool setAutoStartup(const std::string& appName, const std::filesystem::path& exePath);
bool removeAutoStartup(const std::string& appName);
bool createDesktopShortcut(const std::string& appName, const std::filesystem::path& exePath);
bool deleteDesktopShortcut(const std::string& appName);

std::string getCurrentExecutablePath();
std::string getDefaultManifestPath(const std::string& appName, InstallerPathResolver& resolver);
std::string getLocalManifestPath(const std::string& exePath);
bool createUninstallStub(const std::string& sourcePath, const std::string& targetPath);

bool isRunningAsAdmin();
bool requiresAdminForInstall(const std::string& installPath,
                             const ExtendedInstallationMetadata& metadata,
                             InstallerPathResolver& resolver);
bool relaunchSelfAsAdmin();
uint64_t getAvailableDiskSpaceBytes(const std::string& path);
bool checkDiskSpaceForInstall(const std::string& path, uint64_t requiredBytes,
                              uint64_t& availableBytes);
bool checkMinimumWindowsVersion(uint16_t minMajor, uint16_t minMinor, uint32_t minBuild,
                                uint16_t& currentMajor, uint16_t& currentMinor, uint32_t& currentBuild);
bool isProcessRunningByName(const std::string& exeName);
bool terminateProcessByName(const std::string& exeName);
std::string normalizeProcessName(const std::string& name);
std::vector<std::string> buildKillProcessList(const std::string& appName,
                                              const std::vector<std::string>& extraProcesses);
std::vector<std::string> getRunningProcessesByName(const std::vector<std::string>& exeNames);
bool terminateProcessesByName(const std::vector<std::string>& exeNames);

} // namespace MultiThreadedInstaller
