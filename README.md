# Multi-Threaded Installer

A Windows packaging and installation project built with C++17.

Main executables:
- `packager.exe`
- `installer.exe`
- `uninstaller.exe`

## Current Packaging Model

- config file format: YAML only
- supported config filenames:
  - `packager.yaml`
  - `packager.yml`
- required config schema: `schemaVersion: 2`
- each top-level input folder is packaged as one standard folder payload
- supported payload compression:
  - `xz`
  - `zstd`

Old flat config schema and old JSON config files are not supported.

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

## Packager Usage

```powershell
.\build\Release\packager.exe <input_directory> <output_installer.exe>
```

Examples:

```powershell
# XZ/LZMA2
.\build\Release\packager.exe -a xz -l 9 .\input .\dist\MyAppSetup.exe

# ZSTD
.\build\Release\packager.exe -a zstd -l 3 .\input .\dist\MyAppSetup.exe
```

## Config Schema

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

See:
- [packager.yaml](packager.yaml) — 仓库根的配置示例
- [docs/USER_GUIDE.md](docs/USER_GUIDE.md)
- [docs/打包器流程图.md](docs/打包器流程图.md)
- [docs/安装器流程图.md](docs/安装器流程图.md)
