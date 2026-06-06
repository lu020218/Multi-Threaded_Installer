#pragma once

#include <filesystem>
#include <string>

namespace MultiThreadedInstaller {

// Build resources.zip from resourceDir and inject it — plus the uninstaller binary
// (which itself carries RES_ZIP) — into the installer template exe as native PE
// resources of type "BINARY" (names "RES_ZIP" / "UNINSTALLER_EXE").
//
// Must be called on the temp template FILE before any data overlay (metadata/payload)
// is appended: UpdateResource rewrites the PE resource section and does not tolerate a
// trailing overlay. The runtime reads these back via FindResourceA(name, "BINARY").
bool EmbedInstallerPeResources(const std::filesystem::path& installerTemplateExe,
                               const std::filesystem::path& resourceDir,
                               const std::filesystem::path& uninstallerTemplateExe,
                               std::string& error);

} // namespace MultiThreadedInstaller
