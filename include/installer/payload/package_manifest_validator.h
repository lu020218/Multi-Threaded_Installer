#pragma once

#include "common/package_manifest.h"

#include <string>

namespace MultiThreadedInstaller {

/// 校验解码出的 manifest：版本号匹配、identity 完整、各 folder 身份/目标/算法有效且偏移不溢出、
/// hook 配置自洽。失败返回 false 并填充 error。
bool ValidatePackageManifest(const PackageManifest& manifest, std::string& error);
/// 校验运行期元数据：先转成 manifest 校验，再核对 folderCount 与载荷数组一致。

} // namespace MultiThreadedInstaller
