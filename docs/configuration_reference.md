# Packager Configuration File Reference

## Overview

The packager supports configuration files to specify packaging and installation options. This document provides a complete reference for all available configuration options.

## New Schema (packager.json)

```json
{
  "Version": "1.0",
  "AppName": "MyDesktopApp",
  "InstallDir": "%ProgramFiles%",
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

## Configuration File Location

The packager searches for configuration files in the following order:

1. **Environment Variable**: `PACKAGER_CONFIG` - Full path to configuration file
2. **Input Directory**: 
   - `packager.json` (highest priority)
   - `.packager.json` (lower priority)

If no configuration file is found, the packager uses default values.

## File Format

Configuration files must be in JSON format with UTF-8 encoding.

## Configuration Schema

### Root Object

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `Version` | string | No | "1.0" | Configuration file version |
| `AppName` | string | **Yes** | - | Application name (used for directory naming) |
| `InstallDir` | string | No | "%ProgramFiles%" | Suggested default installation directory (without app name) |
| `Folder` | object | No | {} | Folder-level target directory configurations |
| `Registry` | array | No | [] | Registry entries to write after install |
| `AutoStartup` | bool | No | false | Default auto-start behavior |
| `DesktopIcons` | bool | No | false | Default desktop icon behavior |
| `SparseFileThresholdBytes` | number | No | 4194304 | Only files at or above this size are created as sparse files |
| `InstallState` | object | No | { Mode: "Registry", UseMutex: true } | Install state signaling configuration |

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

### LZMA (Default)

- **Value**: `"lzma"`
- **Characteristics**: Higher compression ratio, slower compression/decompression
- **Best for**: Applications where download size is critical

## Complete Configuration Example

```json
{
  "Version": "1.0",
  "AppName": "MyApplication",
  "InstallDir": "%ProgramFiles%",
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
  "AppName": "SimpleApp"
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
  "Folder": {
    "InstallDir": "application"
  }
}
```

Uses LZMA compression for smaller download size at the cost of slower installation.

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

4. **Invalid Characters in Application Name**
   - Application name cannot contain: `\ / : * ? " < > |`

## Error Messages

### Missing Application Name

```
ERROR: Missing required field in configuration file
  File: C:\project\packager.json
  Field: AppName
  Reason: Application name is required
  Suggestion: Add "AppName": "YourAppName" to the configuration file
```

### Folder Not Found

```
ERROR: Configuration validation failed
  File: C:\project\packager.json
  Field: Folder.InstallDir
  Value: "nonexistent"
  Reason: Folder does not exist in input directory
  Suggestion: Verify folder name matches a folder in the input directory
```

## Logging

The packager logs configuration-related information:

### Configuration File Found

```
INFO: Configuration file found: C:\project\packager.json
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
WARNING: Multiple configuration files found, using highest priority: packager.json
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

5. **Use LZMA for most cases**: Unless download size is critical, LZMA provides the best balance

6. **Keep configuration simple**: Only configure folders that need special locations

7. **Document your configuration**: Add comments in a separate README if your configuration is complex

## Troubleshooting

### Configuration file not found

**Problem**: Packager reports "No configuration file found"

**Solutions**:
- Verify file is named `packager.json` or `.packager.json`
- Verify file is in the input directory root
- Check file permissions
- Use `PACKAGER_CONFIG` environment variable to specify full path

### JSON parsing error

**Problem**: "Invalid JSON format" error

**Solutions**:
- Validate JSON syntax using a JSON validator
- Check for missing commas, quotes, or brackets
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
