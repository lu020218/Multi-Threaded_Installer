# Packager Configuration File Reference

## Overview

The packager supports configuration files to specify packaging and installation options. This document provides a complete reference for all available configuration options.

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
| `version` | string | No | "1.0" | Configuration file version |
| `applicationName` | string | **Yes** | - | Application name (used for directory naming) |
| `defaultInstallDirectory` | string | No | "%ProgramFiles%" | Suggested default installation directory (without app name) |
| `compressionAlgorithm` | string | No | "zstd" | Compression algorithm: "zstd" or "lzma" |
| `folderTargets` | array | No | [] | Folder-level target directory configurations |
| `fileMappings` | array | No | [] | File-level mapping rules (optional feature) |

### Folder Target Configuration

Each item in the `folderTargets` array has the following structure:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `folder` | string | **Yes** | Folder name (relative to input directory) |
| `targetDirectory` | string | **Yes** | Target directory type or path |

### File Mapping Rule (Optional)

Each item in the `fileMappings` array has the following structure:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `pattern` | string | **Yes** | File matching pattern (supports wildcards) |
| `targetDirectory` | string | **Yes** | Target directory type or path |
| `subPath` | string | No | "" | Subdirectory within target directory |

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

### ZSTD (Default)

- **Value**: `"zstd"`
- **Characteristics**: Fast compression and decompression, good compression ratio
- **Best for**: General-purpose applications, quick installations

### LZMA

- **Value**: `"lzma"`
- **Characteristics**: Higher compression ratio, slower compression/decompression
- **Best for**: Applications where download size is critical

## Complete Configuration Example

```json
{
  "version": "1.0",
  "applicationName": "MyApplication",
  "defaultInstallDirectory": "%ProgramFiles%",
  "compressionAlgorithm": "zstd",
  "folderTargets": [
    {
      "folder": "app",
      "targetDirectory": "installDirectory"
    },
    {
      "folder": "plugins",
      "targetDirectory": "%AppData%\\Roaming"
    },
    {
      "folder": "config",
      "targetDirectory": "%ProgramData%"
    },
    {
      "folder": "docs",
      "targetDirectory": "%USERPROFILE%\\Documents"
    }
  ],
  "fileMappings": [
    {
      "pattern": "*.config",
      "targetDirectory": "%AppData%\\Roaming",
      "subPath": "Config"
    },
    {
      "pattern": "*.log",
      "targetDirectory": "%LocalAppData%",
      "subPath": "Logs"
    }
  ]
}
```

## Minimal Configuration Example

```json
{
  "applicationName": "SimpleApp"
}
```

This minimal configuration:
- Uses ZSTD compression (default)
- Suggests `%ProgramFiles%\SimpleApp\` as installation directory
- Installs all folders to the user-selected installation directory

## Configuration Scenarios

### Scenario 1: Basic Desktop Application

```json
{
  "applicationName": "MyDesktopApp",
  "defaultInstallDirectory": "%ProgramFiles%",
  "compressionAlgorithm": "zstd",
  "folderTargets": [
    {
      "folder": "bin",
      "targetDirectory": "installDirectory"
    },
    {
      "folder": "resources",
      "targetDirectory": "installDirectory"
    }
  ]
}
```

**Installation Result** (user accepts default):
- `bin/` → `C:\Program Files\MyDesktopApp\bin\`
- `resources/` → `C:\Program Files\MyDesktopApp\resources\`

### Scenario 2: Application with User-Specific Data

```json
{
  "applicationName": "DataApp",
  "defaultInstallDirectory": "%ProgramFiles%",
  "folderTargets": [
    {
      "folder": "program",
      "targetDirectory": "installDirectory"
    },
    {
      "folder": "userdata",
      "targetDirectory": "%AppData%\\Roaming"
    },
    {
      "folder": "cache",
      "targetDirectory": "%LocalAppData%"
    }
  ]
}
```

**Installation Result**:
- `program/` → `C:\Program Files\DataApp\program\`
- `userdata/` → `C:\Users\[User]\AppData\Roaming\DataApp\userdata\`
- `cache/` → `C:\Users\[User]\AppData\Local\DataApp\cache\`

### Scenario 3: Multi-User Application

```json
{
  "applicationName": "SharedApp",
  "defaultInstallDirectory": "%ProgramFiles%",
  "folderTargets": [
    {
      "folder": "app",
      "targetDirectory": "installDirectory"
    },
    {
      "folder": "shared_config",
      "targetDirectory": "%ProgramData%"
    },
    {
      "folder": "user_settings",
      "targetDirectory": "%AppData%\\Roaming"
    }
  ]
}
```

**Installation Result**:
- `app/` → `C:\Program Files\SharedApp\app\`
- `shared_config/` → `C:\ProgramData\SharedApp\shared_config\`
- `user_settings/` → `C:\Users\[User]\AppData\Roaming\SharedApp\user_settings\`

### Scenario 4: High Compression for Distribution

```json
{
  "applicationName": "LargeApp",
  "defaultInstallDirectory": "%ProgramFiles%",
  "compressionAlgorithm": "lzma",
  "folderTargets": [
    {
      "folder": "application",
      "targetDirectory": "installDirectory"
    }
  ]
}
```

Uses LZMA compression for smaller download size at the cost of slower installation.

## Validation Rules

The packager validates configuration files and reports errors for:

1. **Missing Required Fields**
   - `applicationName` is required

2. **Invalid Field Types**
   - All fields must match their specified types

3. **Invalid Values**
   - `compressionAlgorithm` must be "zstd" or "lzma"
   - Folder names must exist in the input directory
   - Target directory paths must be valid

4. **Invalid Characters in Application Name**
   - Application name cannot contain: `\ / : * ? " < > |`

## Error Messages

### Missing Application Name

```
ERROR: Missing required field in configuration file
  File: C:\project\packager.json
  Field: applicationName
  Reason: Application name is required
  Suggestion: Add "applicationName": "YourAppName" to the configuration file
```

### Invalid Compression Algorithm

```
ERROR: Invalid configuration value
  File: C:\project\packager.json
  Field: compressionAlgorithm
  Value: "invalid"
  Reason: Compression algorithm must be "zstd" or "lzma"
  Suggestion: Change to "zstd" or "lzma"
```

### Folder Not Found

```
ERROR: Configuration validation failed
  File: C:\project\packager.json
  Field: folderTargets[0].folder
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
INFO: Compression algorithm: zstd
INFO: Default install directory: %ProgramFiles%
INFO: Folder targets: 3 configured
```

### Using Default Configuration

```
INFO: No configuration file found, using defaults
INFO: Application name: MyApplication (default)
INFO: Compression algorithm: zstd (default)
INFO: Default install directory: %ProgramFiles% (default)
```

### Configuration Warnings

```
WARNING: Unknown configuration field "customField" will be ignored
WARNING: Multiple configuration files found, using highest priority: packager.json
```

## Best Practices

1. **Always specify `applicationName`**: This is required and used for directory naming

2. **Use environment variables for system directories**: Prefer `%ProgramFiles%` over hardcoded paths like `C:\Program Files`

3. **Organize by data type**: 
   - Program files → `installDirectory` or `%ProgramFiles%`
   - User-specific data → `%AppData%\Roaming`
   - Temporary/cache data → `%LocalAppData%`
   - Shared data → `%ProgramData%`

4. **Test with different user selections**: Verify your application works when users choose custom installation directories

5. **Use ZSTD for most cases**: Unless download size is critical, ZSTD provides the best balance

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
- Verify `applicationName` is set correctly
- Check installer logs for path resolution
- Ensure target directory doesn't already contain app name

## See Also

- [Migration Guide](migration_guide.md) - Migrating from command-line arguments
- [Examples](../examples/configurations/) - Additional configuration examples
