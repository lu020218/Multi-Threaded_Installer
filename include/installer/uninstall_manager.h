#pragma once

#include "installer/console_interface.h"
#include "installer/path_resolver.h"

#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

struct UninstallProgressInfo {
    float progress = 0.0f;
    std::string currentItem;
};

using UninstallProgressCallback = std::function<void(const UninstallProgressInfo&)>;
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console);
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console,
                           const UninstallProgressCallback& progressCallback,
                           const std::function<bool()>& cancellationCallback = {});

} // namespace MultiThreadedInstaller
