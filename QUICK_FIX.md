# Quick Fix Notes

This document summarizes the current packaging/runtime assumptions after the recent refactors.

## Current State

- DuiLib is linked statically in the supported build path
- packager embeds UI resources into installer and uninstaller templates
- packager config is YAML only
- required config schema is `schemaVersion: 2`
- old JSON config and old flat config schema are not supported

## Minimal Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_GUI=ON `
  -DSTATIC_LINK_RUNTIME=ON `
  -DENABLE_ZSTD=ON

cmake --build build --config Release --target installer
cmake --build build --config Release --target uninstaller
cmake --build build --config Release --target packager
```

## Minimal Packaging Flow

1. Put your app folders and `packager.yaml` in one input directory.
2. Run:

```powershell
build\Release\packager.exe <input_directory> <output_installer.exe>
```

3. The generated installer will contain:
- installer metadata
- folder payloads
- embedded UI resources
- embedded `uninstaller.exe`

## Config Reminder

Required structure:

```yaml
schemaVersion: 2

app:
package:
install:
ui:
layout:
lifecycle:
```

Reference:
- [examples/packager.yaml](/e:/Work/GitHub/Multi-Threaded_Installer-master/examples/packager.yaml)
