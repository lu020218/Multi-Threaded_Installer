# User Guide

## Overview

This project uses `packager.exe` to turn a payload input directory plus a config directory into a GUI installer.

Current configuration rules:
- configuration file format: YAML only
- supported filenames: `packager.yaml`, `packager.yml`
- required schema: `schemaVersion: 2`
- older schemas are not supported

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

Recommended payload input directory:

```text
input/
├─ bin/
└─ plugins/
```

Recommended config directory:

```text
build-config/
├─ packager.yaml
├─ app.ico
└─ resources/
```

Each top-level folder listed in `layout.folders` becomes one compressed folder payload in the package.

## Packager Usage

```powershell
.\build\Release\packager.exe --input <input_directory> --config <config_directory> --output <output_installer.exe>
```

Examples:

```powershell
.\build\Release\packager.exe --input .\input --config .\build-config --output .\dist\MyAppSetup.exe
.\build\Release\packager.exe -o .\dist\MyAppSetup.exe -c .\build-config -i .\input
```

Notes:
- `packager.exe` reads `packager.yaml`, `resources/`, and relative icon paths from the config directory.
- `packager.exe` reads installer and uninstaller templates from its own directory.
- generated installers embed UI resources and an embedded uninstaller
- `packager.yaml` controls packaging, install defaults, UI metadata, layout, and lifecycle behaviors

## Installer Usage

GUI install:

```powershell
.\dist\MyAppSetup.exe
```

Silent install:

```powershell
.\dist\MyAppSetup.exe --silent
```

Silent install with destination and components:

```powershell
.\dist\MyAppSetup.exe --silent `
  --destination "C:\Program Files\MyApp" `
  --components all `
  --auto-startup true `
  --desktop-icon false
```

Silent uninstall:

```powershell
.\dist\uninstall.exe --silent
```

## packager.yaml Schema

Top-level blocks:

```yaml
schemaVersion: 2

app:
package:
install:
ui:
layout:
lifecycle:
```

### app

Product identity and version metadata.

```yaml
app:
  name: MyDesktopApp
  id: my_desktop_app
  version: 1.2.3
  directoryName: MyDesktopApp
  website: https://example.com
  product:
    icon: app.ico
    productName: MyDesktopApp
    fileVersion: 1.2.3.4
    productVersion: 1.2.3.4
    companyName: MyCompany
    fileDescription: MyDesktopApp Installer
    copyright: Copyright (c) 2026 MyCompany
```

### package

Compression behavior.

```yaml
package:
  compression:
    algorithm: xz   # xz | zstd
    level: 9
    threads: auto   # auto | integer
```

### install

Installer defaults and install-state behavior.

```yaml
install:
  defaultDir: "%ProgramFiles%\\MyDesktopApp"
  requireAdmin: true
  autoCleanOldInstall: true
  autoStartup: false
  desktopIcon: true
  minWindows:
    major: 10
    minor: 0
    build: 19045
  sparseFileThresholdBytes: 4194304
  killProcesses:
    - MyDesktopApp.exe
  installState:
    mode: registry   # registry | file | both
    registryPath: HKEY_CURRENT_USER\\Software\\my_desktop_app
    registryKey: InstallState
    filePath: "%ProgramData%\\my_desktop_app\\install.state"
    useMutex: true
    mutexName: Global\\my_desktop_app_Install
```

### ui

Display language, shortcut display names, links, and component selection UI binding.

```yaml
ui:
  defaultLanguage: zh_CN
  desktopShortcut:
    defaultName: MyDesktopApp
    i18n:
      zh_CN: My Desktop App
      en_US: MyDesktopApp
  links:
    - control: btnChrome
      url: https://example.com/chrome-plugin
  componentSelection:
    mode: embeddedInExistingPages
    binding:
      strategy: xml_userdata
      tokenPrefix: "component:"
      pages:
        - skin: welcome_page.xml
          controls:
            - chkChrome
```

### layout

Folder payloads and optional components.

```yaml
layout:
  folders:
    - id: bin
      source: bin
      destination:
        type: install   # install | programFiles | programFilesX86 | appDataRoaming | appDataLocal | programData | userProfile | custom
        appendDirectoryName: false

    - id: plugins
      source: plugins
      destination:
        type: appDataRoaming
        appendDirectoryName: true

  components:
    - id: main_app
      name: Main Application
      required: true
      defaultSelected: true
      folders:
        - bin
        - plugins
      source:
        type: embedded
```

### lifecycle

Compatibility IDs, install-time registry writes, upgrade cleanup, uninstall cleanup, and post-setup agent configuration.

```yaml
lifecycle:
  compatibility:
    legacyAppIds:
      - MyDesktopAppLegacy
    legacyDesktopShortcutNames:
      - MyDesktopApp Legacy

  registry:
    onInstall:
      - path: HKEY_CURRENT_USER\\Software\\my_desktop_app
        key: InstallDir
        value: "%InstallDir%"
        type: expand

  cleanup:
    onUpgrade:
      registry:
        deleteFromManifest: true
        legacyKeys: []
      extraPaths: []
    onUninstall:
      paths: []

```

## Validation Rules

Required fields:
- `schemaVersion`
- `app.name`
- `app.version`
- `install.defaultDir`
- `layout.folders`

Important validation rules:
- `schemaVersion` must be `2`
- `package.compression.algorithm` must be `xz` or `zstd`
- `layout.folders[].id` must be unique
- `layout.components[].folders[]` must reference declared folder IDs
- `layout.folders[].destination.type` must be a supported destination type

## Troubleshooting

### Missing required field

Example:

```text
Missing required field 'app.name'
```

Fix the path exactly as reported in the error.

### Invalid destination type

Example:

```text
Invalid field 'layout.folders[].destination.type'
```

Use one of:
- `install`
- `programFiles`
- `programFilesX86`
- `appDataRoaming`
- `appDataLocal`
- `programData`
- `userProfile`
- `custom`

For content that should live under the current user's Roaming profile, prefer `appDataRoaming`.
Do not use `custom` with `%AppData%\\Roaming`, because `%AppData%` already resolves to the Roaming directory.

### Input folder not found

The `layout.folders[].source` path is resolved relative to the input directory passed to `packager.exe`.

### Icon file not found

`app.product.icon` is resolved relative to the config directory unless an absolute path is provided.

## Reference Example

See:

- [examples/packager.yaml](/e:/Work/GitHub/Multi-Threaded_Installer-master/examples/packager.yaml)
