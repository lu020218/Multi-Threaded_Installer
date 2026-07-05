#pragma once

#include <string>

namespace MultiThreadedInstaller {

/// 写入安装器 exe 的版本资源字段。由引擎从 version/publisher/productName 派生
/// （FileVersion/ProductVersion 去预发布后缀为纯数字四段；copyright 缺省=publisher+年份）。
struct VersionInfoData {
    std::string productName;       ///< 产品名。
    std::string fileVersion;       ///< 文件版本（纯数字四段，如 7.0.0.0）。
    std::string productVersion;    ///< 产品版本（同上）。
    std::string companyName;       ///< 公司名（=publisher）。
    std::string fileDescription;   ///< 文件描述。
    std::string copyright;         ///< 版权信息。
    std::string originalFilename;  ///< 原始文件名。
};

/// 把版本资源写入安装器 exe 的 PE VERSIONINFO（内部走带重试的资源会话）。失败返回 false + error。
bool UpdateInstallerVersionInfo(const std::string& exePath, const VersionInfoData& info, std::string& error);
/// 设置安装器 exe 的清单执行级别（requireAdmin=true 写 requireAdministrator，带重试）。失败返回 false + error。
bool UpdateInstallerExecutionLevel(const std::string& exePath, bool requireAdmin, std::string& error);

/// 仅在「已打开的资源更新会话句柄」上写入版本资源（不 Begin/End），供单会话编排复用。
bool ApplyInstallerVersionInfoInto(void* update, const VersionInfoData& info, std::string& error);
/// 仅在「已打开的资源更新会话句柄」上写入清单执行级别（不 Begin/End），供单会话编排复用。
bool ApplyInstallerManifestInto(void* update, bool requireAdmin, std::string& error);

} // namespace MultiThreadedInstaller
