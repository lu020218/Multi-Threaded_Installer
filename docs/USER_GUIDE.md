# User Guide

## Overview

`packager.exe` reads a payload input directory and a config directory, then generates a self-extracting GUI installer.

Current configuration rules:
- configuration file format: YAML
- supported filenames: `packager.yaml`, `packager.yml`
- required schema: `schemaVersion: 3`
- v2 and older schemas are not compatible and fail at packager configuration loading

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_GUI=ON `
  -DSTATIC_LINK_RUNTIME=ON `
  -DENABLE_ZSTD=ON

cmake --build build --config Release
```

Build outputs:
- `build/Release/packager.exe`
- `build/Release/installer.exe`
- `build/Release/uninstaller.exe`

## Directory Layout

Payload input directory:

```text
payload/
├─ app/
├─ resources/
└─ plugins/
```

Config directory:

```text
build-config/
├─ packager.yaml
├─ app.ico
└─ resources/
```

`installer.payload[].source` is resolved relative to `--input`. `packager.yaml`, UI `resources/`, and relative `app.icon` are resolved relative to `--config`.

## Packager Usage

```powershell
.\build\Release\packager.exe --input <input_directory> --config <config_directory> --output <output_installer.exe>
```

Named arguments can be passed in any order:

```powershell
.\build\Release\packager.exe -o .\dist\MyAppSetup.exe -c .\build-config -i .\payload
```

Only these public arguments are supported:
- `--input` / `-i`
- `--config` / `-c`
- `--output` / `-o`
- `--help` / `-h`

## Installer Usage

GUI install:

```powershell
.\dist\MyAppSetup.exe
```

Silent install:

```powershell
.\dist\MyAppSetup.exe --silent
```

Silent install with overrides:

```powershell
.\dist\MyAppSetup.exe --silent `
  --destination "C:\Program Files\MyApp" `
  --components all `
  --auto-startup true `
  --desktop-icon false
```

Upgrade install:

```powershell
.\dist\MyAppSetup.exe --upgrade
.\dist\MyAppSetup.exe --upgrade --silent
```

Silent uninstall:

```powershell
.\dist\uninstall.exe --silent
```

## packager.yaml Schema

Top-level blocks:

```yaml
schemaVersion: 3

app:
package:
installer:
uninstaller:
```

### app

Product identity, icon, and Windows version resource metadata.

```yaml
app:
  id: my_desktop_app
  name: MyDesktopApp
  version: 1.2.3
  publisher: MyCompany
  website: https://example.com
  icon: app.ico
  versionInfo:
    productName: MyDesktopApp
    fileDescription: MyDesktopApp Installer
    fileVersion: 1.2.3.4
    productVersion: 1.2.3.4
    companyName: MyCompany
```

### package

Compression behavior.

```yaml
package:
  compression:
    algorithm: xz
    level: 9
```

### installer

Installer policy, UI defaults, payload, components, registry writes, system uninstall entry, and installState persistence.

```yaml
installer:
  requireAdmin: true
  defaultDir: "%ProgramFiles%\\MyDesktopApp"
  directoryName: MyDesktopApp
  minWindows: "10.0.19041"
  mutex: "Global\\my_desktop_app_Install"
  defaults:
    autoStartup: true
    desktopShortcut: true
  installState:
    registries:
      - id: main
        path: "HKCU\\Software\\my_desktop_app"
        values:
          installDir:
            key: InstallDir
            value: "%InstallDir%"
            type: expand
          installState:
            key: InstallState
            value: "%InstallState%"
            type: string
    files:
      - id: main
        path: "%ProgramData%\\my_desktop_app\\install-state.json"
        format: json
        values:
          installDir:
            name: installDir
            value: "%InstallDir%"
    detect:
      primary:
        registry: main
        value: installDir
      legacy:
        - id: legacy_v2
          path: "HKCU\\Software\\OldCompany\\OldApp"
          installDirValue: InstallPath
  systemUninstallEntry:
    scope: machine
    displayName: MyDesktopApp
    publisher: MyCompany
  cleanup:
    systemUninstallEntry:
      legacyEntries:
        - displayName: Old Desktop App
          scope: user
  payload:
    - id: app
      source: app
      target: "%InstallDir%"
      required: true
  components:
    - id: core
      name: Core Application
      required: true
      defaultSelected: true
      payload:
        - app
```

### uninstaller

Cleanup policy, process closing, and uninstall UI. Installed-instance discovery is configured under `installer.installState.detect`.

```yaml
uninstaller:
  requireAdmin: true
  killBeforeUninstall:
    - MyDesktopApp.exe
  cleanup:
    installedFiles: manifest
    missingManifestFallback: safeDirectoryFallback
    installState: delete
    autoStartup: auto
    desktopShortcut: auto
    systemUninstallEntry:
      scope: machine
      displayName: MyDesktopApp
      legacyEntries:
        - displayName: Old Desktop App
          scope: user
    paths:
      - path: "%LocalAppData%\\MyDesktopApp\\Cache"
        recursive: true
        onlyIfEmpty: false
```

## Validation Rules

Required fields:
- `schemaVersion`
- `app.id`
- `app.name`
- `app.version`
- `installer.defaultDir`
- `installer.directoryName`
- `installer.payload[].id`
- `installer.payload[].source`
- `installer.payload[].target`
- `installer.installState.detect.primary` or `installer.installState.detect.legacy[]`

Important validation rules:
- `schemaVersion` must be `3`.
- `installer.payload[].source` must exist under the input directory.
- `installer.components[].payload[]` must reference declared payload ids.
- component dependencies must not contain cycles.
- `installer.installState.registries[]` and `installer.installState.files[]` ids must be unique.
- `installer.installState.detect.primary.registry/value` must reference a configured registry store and logical value.
- `installer.installState.detect.legacy[].id` must be unique; detection tries primary first, then legacy entries in order.
- `installer.requireAdmin=false` rejects Program Files defaults, HKLM registry writes, and machine/both system uninstall entries.

## Troubleshooting

### Unsupported schema

```text
Unsupported schemaVersion. Only schemaVersion 3 is supported.
```

Convert the configuration to the v3 `app/package/installer/uninstaller` structure.

### Missing payload source

`installer.payload[].source` is resolved relative to the input directory passed to `packager.exe`.

### Icon file not found

`app.icon` is resolved relative to the config directory unless an absolute path is provided.

## Reference Example

See:

- [examples/packager.yaml](/e:/Work/GitHub/Multi-Threaded_Installer-master/examples/packager.yaml)
- [docs/packager-yaml-v3.example.yaml](/e:/Work/GitHub/Multi-Threaded_Installer-master/docs/packager-yaml-v3.example.yaml)
