# Requirements

## Goal

This project provides a Windows packaging and installation solution with three executables:
- `packager.exe`
- `installer.exe`
- `uninstaller.exe`

Current architecture assumptions:
- packager input is configured by `packager.yaml` or `packager.yml`
- configuration schema is `schemaVersion: 2`
- old JSON config and old flat schema are not supported
- each top-level input folder is packaged as a single standard folder payload
- folder payload compression supports `XZ/LZMA2` and `ZSTD`

## Functional Requirements

### Packager

- scan an input directory and find top-level folders to package
- read `packager.yaml` / `packager.yml`
- validate configuration against schema version 2
- compress each folder as one standard payload
- generate installer metadata
- embed metadata and payloads into the final installer
- optionally generate an external data package
- apply icon and version resources when configured

### Installer

- GUI install
- silent install
- GUI uninstall
- silent uninstall
- component-aware install
- folder payload extraction and tar stream file write
- install-state tracking
- registry writes, shortcut creation, startup item creation
- upgrade cleanup and uninstall cleanup

### Configuration Schema

Required top-level structure:

```yaml
schemaVersion: 2

app:
package:
install:
ui:
layout:
lifecycle:
```

Required fields:
- `schemaVersion`
- `app.name`
- `app.version`
- `install.defaultDir`
- `layout.folders`

Supported compression algorithms:
- `xz`
- `zstd`

### Layout and Components

- `layout.folders[].id` must be unique
- `layout.folders[].source` must exist under input directory
- `layout.folders[].destination.type` must be valid
- `layout.components[].folders[]` must reference declared folder ids
- component dependency graph must be acyclic
- local component installers must stay under install directory
- download component installers must use `https://` and valid SHA256

## Non-Functional Requirements

- Windows-focused build and runtime
- maintainable module boundaries
- predictable logging
- explicit validation errors
- no hidden compatibility branches for old package schema

## Build Requirements

- CMake-based build
- Visual Studio 2022 / MSVC on Windows
- `yaml-cpp`, `liblzma`, `zstd`, and DuiLib dependencies available in `third_party`

Build products:
- `build/Release/packager.exe`
- `build/Release/installer.exe`
- `build/Release/uninstaller.exe`

## Acceptance Criteria

- packager loads a valid schema v2 YAML file and produces an installer
- installer can install payloads produced by the current packager
- uninstaller can remove installed files and cleanup artifacts
- invalid config fails with explicit field-path errors
- no old flat schema or JSON config path remains in public documentation
