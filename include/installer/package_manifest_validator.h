#pragma once

#include "common/package_manifest.h"

#include <string>

namespace MultiThreadedInstaller {

bool ValidatePackageManifest(const PackageManifest& manifest, std::string& error);
bool ValidateExtendedInstallationMetadata(const ExtendedInstallationMetadata& metadata,
                                          std::string& error);

} // namespace MultiThreadedInstaller
