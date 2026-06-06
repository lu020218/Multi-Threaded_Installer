#pragma once

#include "common/package_manifest.h"

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

std::vector<uint8_t> SerializePackageManifest(const PackageManifest& manifest);

// When deferFileIndex is true, the per-file fileIndex arrays in the payload section are
// skipped (left empty). Use this for callers that only need folder/scalar metadata (e.g.
// the installer GUI) — the install path re-parses with deferFileIndex=false to get them.
bool DeserializePackageManifest(const std::vector<uint8_t>& data,
                                PackageManifest& manifest,
                                std::string& error,
                                bool deferFileIndex = false);

} // namespace MultiThreadedInstaller
