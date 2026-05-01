#pragma once

#include "common/package_manifest.h"

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

std::vector<uint8_t> SerializePackageManifest(const PackageManifest& manifest);

bool DeserializePackageManifest(const std::vector<uint8_t>& data,
                                PackageManifest& manifest,
                                std::string& error);

} // namespace MultiThreadedInstaller
