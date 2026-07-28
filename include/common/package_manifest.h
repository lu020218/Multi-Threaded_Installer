#pragma once

// PackageManifest（全工程唯一元数据根）及其组成部件已收敛：
//   PackageIdentity / HookScript / PackageHooks → config_types.h
//   PackagePayloadFolder / PackagePayloadManifest / PackageManifest → archive_types.h
// 本头保留为兼容转发（codec 等处仍以 package_manifest.h 为语义入口）。
#include "common/archive_types.h"
