# Packager Configuration File Reference

## Overview

The packager supports configuration files to specify packaging and installation options.  
This document provides a complete reference for all available configuration options.

## Supported Formats

### JSON (legacy-compatible schema)

```json
{
  "Version": "1.0",
  "AppName": "MyDesktopApp",
  "InstallDir": "%ProgramFiles%",
  "compressionAlgorithm": "zstd",
  "compressionLevel": 3,
  "Folder": {
    "InstallDir": "bin",
    "Roaming": "plugins",
    "Local": "userdata"
  },
  "Registry": [{
    "key": "InstallDir",
    "value": "%InstallDir%",
    "path": "HKEY_CURRENT_USER\\Software\\MyDesktopApp",
    "type": "expand"
  },
  {
    "key": "Version",
    "value": "%Version%",
    "path": "HKEY_CURRENT_USER\\Software\\MyDesktopApp"
  }],
  "AutoStartup": false,
  "DesktopIcons": true,
  "AutoCleanOldInstall": false,
  "RequireAdmin": false,
  "MinWindowsVersion": "10.0.19041",
  "SparseFileThresholdBytes": 4194304,
  "InstallState": {
    "Mode": "Registry",
    "RegistryPath": "HKEY_CURRENT_USER\\Software\\MyDesktopApp",
    "RegistryKey": "InstallState",
    "FilePath": "%ProgramData%\\MyDesktopApp\\install.state",
    "UseMutex": true,
    "MutexName": "Global\\MyDesktopApp_Install"
  }
}
```

### YAML (same semantics as JSON)

```yaml
Version: "1.0"
AppName: "MyDesktopApp"
InstallDir: "%ProgramFiles%"
compressionAlgorithm: "zstd"
compressionLevel: 3
Folder:
  InstallDir: "bin"
  Roaming: "plugins"
  Local: "userdata"
AutoStartup: false
DesktopIcons: true
AutoCleanOldInstall: false
RequireAdmin: false
MinWindowsVersion: "10.0.19041"
SparseFileThresholdBytes: 4194304
```

The loader also accepts a structured YAML style and maps it to the same runtime fields, for example:
- `package.appName` -> `AppName`
- `package.version` -> `Version`
- `install.defaultInstallDir` -> `InstallDir`
- `install.installState.*` -> `InstallState.*`
- `install.killProcesses` -> `KillProcesses`
- `folders[]` -> `Folder` (`InstallDir` / `Roaming` / `Local`)

## Configuration File Location

The packager searches for configuration files in the following order:

1. **Environment Variable**: `PACKAGER_CONFIG` - Full path to configuration file
2. **Input Directory**: 
   - `packager.yaml` (highest priority)
   - `packager.yml`
   - `packager.json`
   - `.packager.json`

If no configuration file is found, the packager uses default values.

## File Format

Configuration files must be UTF-8 encoded JSON or YAML.

## Configuration Schema

### Root Object

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Version` | string | No | "1.0" | Configuration file version |
| `AppName` | string | **Yes** | - | Application name (used for directory naming) |
| `InstallDir` | string | No | "%ProgramFiles%" | Suggested default installation directory (without app name) |
| `compressionAlgorithm` | string | No | `"lzma"` | Compression algorithm: `lzma` or `zstd` |
| `compressionLevel` | number | No | `-1` | Compression level (`-1` = use algorithm default) |
| `Folder` | object | No | {} | Folder-level target directory configurations |
| `Registry` | array | No | [] | Registry entries to write after install |
| `AutoStartup` | bool | No | false | Default auto-start behavior |
| `DesktopIcons` | bool | No | false | Default desktop icon behavior |
| `AutoCleanOldInstall` | bool | No | false | Automatically clean previous install when target directory changes |
| `RequireAdmin` | bool | No | false | Require administrator privileges at installer startup |
| `MinWindowsVersion` | string | No | - | Minimum Windows version (format: "major.minor.build") |
| `SparseFileThresholdBytes` | number | No | 4194304 | Only files at or above this size are created as sparse files |
| `InstallState` | object | No | { Mode: "Registry", UseMutex: true } | Install state signaling configuration |
| `components` | array | No | [] | Optional component definitions (embedded/local/download) |
| `ui.componentSelection` | object | No | defaults applied | Optional component binding metadata for GUI selection |

### Folder Target Configuration

`Folder` supports these keys:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `InstallDir` | string | No | Folder to install under the main install directory |
| `Roaming` | string | No | Folder to install under %AppData%\\Roaming |
| `Local` | string | No | Folder to install under %LocalAppData% |

### Registry Entries

Each item in `Registry` supports:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `path` | string | **Yes** | Registry path, e.g. `HKEY_CURRENT_USER\\Software\\MyApp` |
| `key` | string | **Yes** | Value name |
| `value` | string or number | **Yes** | Value data (supports `%InstallDir%`, `%Version%`, `%AppName%`) |
| `type` | string | No | `string` (default), `dword`, `expand` |

### Install State

`InstallState` supports:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `Mode` | string | No | `Registry`, `File`, or `Both` |
| `RegistryPath` | string | No | Path for install state registry value |
| `RegistryKey` | string | No | Value name for install state |
| `FilePath` | string | No | File path for install state |
| `UseMutex` | bool | No | Whether to create a named mutex during install |
| `MutexName` | string | No | Named mutex |

### Components (v13 metadata capable)

`components[]` supports:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | No* | Component identifier |
| `name` | string | No | Display name |
| `description` | string | No | Display description |
| `required` | bool | No | Whether component is mandatory |
| `defaultSelected` | bool | No | Whether component is selected by default |
| `dependsOn` | array[string] | No | Dependency component IDs |
| `folders` | array[string] | No | Related embedded folder names |
| `source` | object | No | Installer source settings (`embedded` / `local` / `download`) |
| `registry` | array | No | Component-scoped registry entries |
| `killProcesses` | array[string] | No | Component-scoped process list |
| `createDesktopShortcut` | bool | No | Component-scoped shortcut preference |
| `autoStartup` | bool | No | Component-scoped autostart preference |

\* Validation and strict schema enforcement are handled separately.

`source` supports:
- `type`: `embedded` / `local` / `download`
- `local`: `base`, `installer`, `args`, `wait`, `timeoutSec`, `uninstall`
- `download`: `url`, `sha256`, `saveAs`, `args`, `wait`, `timeoutSec`, `uninstall`

### UI Component Binding

`ui.componentSelection` supports:
- `mode`: `dedicatedPage` / `embeddedInExistingPages` / `hybrid`
- `binding.strategy`: e.g. `xml_userdata`
- `binding.tokenPrefix`: e.g. `component:`
- `binding.pages[]`: each page with `skin` and `controls[]`

## Target Directory Values

### Special Directory Types

| Value | Description | Example Final Path |
|-------|-------------|-------------------|
| `installDirectory` | User-selected installation directory | `C:\Program Files\MyApp\` |
| `%ProgramFiles%` | Program Files directory | `C:\Program Files\MyApp\` |
| `%ProgramFiles(x86)%` | Program Files (x86) directory | `C:\Program Files (x86)\MyApp\` |
| `%AppData%` or `%AppData%\Roaming` | User Roaming AppData | `C:\Users\[User]\AppData\Roaming\MyApp\` |
| `%LocalAppData%` | User Local AppData | `C:\Users\[User]\AppData\Local\MyApp\` |
| `%ProgramData%` | Shared application data | `C:\ProgramData\MyApp\` |
| `%USERPROFILE%` | User profile directory | `C:\Users\[User]\MyApp\` |

**Important**: All paths automatically append the application name during installation.

### Path Resolution Behavior

The installer intelligently handles application name appending:

1. **User selects**: `C:\Program Files\`
   - **Result**: `C:\Program Files\MyApp\` (app name appended)

2. **User selects**: `C:\Program Files\MyApp\`
   - **Result**: `C:\Program Files\MyApp\` (app name already present, not duplicated)

3. **Environment variable**: `%AppData%`
   - **Expands to**: `C:\Users\[User]\AppData\Roaming\`
   - **Result**: `C:\Users\[User]\AppData\Roaming\MyApp\` (app name appended)

## Compression Algorithms

### `lzma` (Default)

- **Value**: `"lzma"`
- **Level range**: `0-9`
- **Default level**: `9` (when `compressionLevel = -1`)
- **Characteristics**: Higher compression ratio, slower compression/decompression

### `zstd`

- **Value**: `"zstd"`
- **Level range**: `1-22`
- **Default level**: `3` (when `compressionLevel = -1`)
- **Characteristics**: Faster compression/decompression with good ratio

### Notes

- `compressionLevel = -1` means "not explicitly set", runtime chooses per-algorithm default.
- CLI level (if passed) overrides config level.
- CLI algorithm (if passed) overrides config algorithm.

## Complete Configuration Example

```json
{
  "Version": "1.0",
  "AppName": "MyApplication",
  "InstallDir": "%ProgramFiles%",
  "compressionAlgorithm": "zstd",
  "compressionLevel": 3,
  "Folder": {
    "InstallDir": "bin",
    "Roaming": "plugins",
    "Local": "userdata"
  },
  "Registry": [{
    "key": "InstallDir",
    "value": "%InstallDir%",
    "path": "HKEY_CURRENT_USER\\Software\\MyApplication"
  },
  {
    "key": "Version",
    "value": "%Version%",
    "path": "HKEY_CURRENT_USER\\Software\\MyApplication"
  }],
  "AutoStartup": false,
  "DesktopIcons": true,
  "AutoCleanOldInstall": false,
  "RequireAdmin": false,
  "MinWindowsVersion": "10.0.19041",
  "SparseFileThresholdBytes": 4194304,
  "InstallState": {
    "Mode": "Registry",
    "RegistryPath": "HKEY_CURRENT_USER\\Software\\MyApplication",
    "RegistryKey": "InstallState",
    "FilePath": "%ProgramData%\\MyApplication\\install.state",
    "UseMutex": true,
    "MutexName": "Global\\MyApplication_Install"
  }
}
```

## Minimal Configuration Example

```json
{
  "AppName": "SimpleApp",
  "compressionAlgorithm": "lzma",
  "compressionLevel": -1
}
```

This minimal configuration:
- Uses LZMA compression (default)
- Suggests `%ProgramFiles%\SimpleApp\` as installation directory
- Installs all folders to the user-selected installation directory

## Configuration Scenarios

### Scenario 1: Basic Desktop Application

```json
{
  "Version": "1.0",
  "AppName": "MyDesktopApp",
  "InstallDir": "%ProgramFiles%",
  "Folder": {
    "InstallDir": "bin"
  }
}
```

**Installation Result** (user accepts default):
- `bin/` -> `C:\Program Files\MyDesktopApp\bin\`

### Scenario 2: Application with User-Specific Data

```json
{
  "Version": "1.0",
  "AppName": "DataApp",
  "InstallDir": "%ProgramFiles%",
  "Folder": {
    "InstallDir": "program",
    "Roaming": "userdata",
    "Local": "cache"
  }
}
```

**Installation Result**:
- `program/` -> `C:\Program Files\DataApp\program\`
- `userdata/` -> `C:\Users\[User]\AppData\Roaming\DataApp\userdata\`
- `cache/` -> `C:\Users\[User]\AppData\Local\DataApp\cache\`

### Scenario 3: Multi-User Application

```json
{
  "Version": "1.0",
  "AppName": "SharedApp",
  "InstallDir": "%ProgramFiles%",
  "Folder": {
    "InstallDir": "app",
    "Roaming": "user_settings"
  }
}
```

**Installation Result**:
- `app/` -> `C:\Program Files\SharedApp\app\`
- `user_settings/` -> `C:\Users\[User]\AppData\Roaming\SharedApp\user_settings\`

### Scenario 4: High Compression for Distribution

```json
{
  "Version": "1.0",
  "AppName": "LargeApp",
  "InstallDir": "%ProgramFiles%",
  "compressionAlgorithm": "zstd",
  "compressionLevel": 3,
  "Folder": {
    "InstallDir": "application"
  }
}
```

Uses ZSTD compression for a faster install path with good compression ratio.

## Validation Rules

The packager validates configuration files and reports errors for:

1. **Missing Required Fields**
   - `AppName` is required

2. **Invalid Field Types**
   - All fields must match their specified types

3. **Invalid Values**
   - Folder names must exist in the input directory
   - Target directory paths must be valid
   - InstallState fields must be valid when provided
   - `compressionAlgorithm` must be `lzma` or `zstd`
   - `compressionLevel` must match algorithm range (`lzma: 0-9`, `zstd: 1-22`, or `-1`)

4. **Invalid Characters in Application Name**
   - Application name cannot contain: `\ / : * ? " < > |`

5. **Component Rules (when `components` is provided)**
   - `components[].id` must be unique
   - `dependsOn` must reference existing component IDs
   - dependency graph must be acyclic
   - `required: true` cannot use `defaultSelected: false`
   - `source.local.base` must be under `%InstallDir%` / `installDirectory`
   - `source.local` paths cannot contain parent traversal (`..`)
   - `source.download.url` must use `https://`
   - `source.download.sha256` must be a 64-character hex digest

## Error Messages

### Missing Application Name

```
ERROR: Missing required field in configuration file
  File: C:\project\packager.yaml
  Field: AppName
  Reason: Application name is required
  Suggestion: Add "AppName": "YourAppName" to the configuration file
```

### Folder Not Found

```
ERROR: Configuration validation failed
  File: C:\project\packager.yaml
  Field: Folder.InstallDir
  Value: "nonexistent"
  Reason: Folder does not exist in input directory
  Suggestion: Verify folder name matches a folder in the input directory
```

## Logging

The packager logs configuration-related information:

### Configuration File Found

```
INFO: Configuration file found: C:\project\packager.yaml
INFO: Application name: MyApplication
INFO: Default install directory: %ProgramFiles%
INFO: Folder targets: 3 configured
```

### Using Default Configuration

```
INFO: No configuration file found, using defaults
INFO: Application name: MyApplication (default)
INFO: Default install directory: %ProgramFiles% (default)
```

### Configuration Warnings

```
WARNING: Unknown configuration field "customField" will be ignored
WARNING: Multiple configuration files found, using highest priority: packager.yaml
```

## Best Practices

1. **Always specify `AppName`**: This is required and used for directory naming

2. **Use environment variables for system directories**: Prefer `%ProgramFiles%` over hardcoded paths like `C:\Program Files`

3. **Organize by data type**: 
   - Program files -> `%ProgramFiles%`
   - User-specific data -> `%AppData%\Roaming`
   - Temporary/cache data -> `%LocalAppData%`
   - Shared data -> `%ProgramData%`

4. **Test with different user selections**: Verify your application works when users choose custom installation directories

5. **Choose algorithm by target**:
   - Prefer `zstd` for faster compression/decompression
   - Prefer `lzma` when maximum compression ratio is the priority

6. **Keep configuration simple**: Only configure folders that need special locations

7. **Document your configuration**: Add comments in a separate README if your configuration is complex

## Troubleshooting

### Configuration file not found

**Problem**: Packager reports "No configuration file found"

**Solutions**:
- Verify file is named `packager.yaml`, `packager.yml`, `packager.json`, or `.packager.json`
- Verify file is in the input directory root
- Check file permissions
- Use `PACKAGER_CONFIG` environment variable to specify full path

### Config parsing error

**Problem**: "Invalid JSON/YAML format" error

**Solutions**:
- Validate JSON or YAML syntax with a validator
- Check for missing commas/quotes/brackets (JSON) or indentation errors (YAML)
- Ensure UTF-8 encoding without BOM

### Folder not found error

**Problem**: "Folder does not exist in input directory"

**Solutions**:
- Verify folder name matches exactly (case-sensitive on some systems)
- Check folder path is relative to input directory root
- Ensure folder exists before running packager

### Application name not appended

**Problem**: Files installed to wrong location

**Solutions**:
- Verify `AppName` is set correctly
- Check installer logs for path resolution
- Ensure target directory doesn't already contain app name

## See Also

- [Migration Guide](migration_guide.md) - Migrating from command-line arguments
- [Examples](../examples/configurations/) - Additional configuration examples
- [Componentized Install Troubleshooting Guide](components_troubleshooting_guide.md) - Debugging component selection/install/uninstall issues
