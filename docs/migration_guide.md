# Migration Guide: Command-Line to Configuration File

## Overview

This guide helps you migrate from the old command-line argument-based packager to the new configuration file-based approach. The new system simplifies the command-line interface while providing more flexibility through configuration files.

## What Changed

### Old System (Command-Line Arguments)

The old packager accepted multiple command-line arguments:

```bash
packager.exe <input_directory> <output_file> [options]
  --compression <algorithm>
  --install-dir <directory>
  --app-name <name>
  --folder-target <folder>:<target>
  ...
```

### New System (Configuration File)

The new packager accepts only two arguments:

```bash
packager.exe <input_directory> <output_file>
```

All other options are specified in a configuration file (`packager.json` or `.packager.json`) in the input directory.

## Benefits of Configuration Files

1. **Simpler Command Line**: Only two arguments needed
2. **Reusable Configurations**: Save and reuse configurations across builds
3. **Version Control**: Track configuration changes in your repository
4. **Better Organization**: All packaging options in one place
5. **Validation**: Configuration is validated before packaging starts
6. **Documentation**: Configuration files are self-documenting

## Migration Steps

### Step 1: Identify Your Current Command-Line Arguments

Review your current packager invocation and identify all arguments you're using.

**Example old command**:
```bash
packager.exe C:\MyProject\dist C:\Output\installer.exe ^
  --compression zstd ^
  --app-name MyApplication ^
  --install-dir "%ProgramFiles%" ^
  --folder-target app:installDirectory ^
  --folder-target plugins:%AppData%\Roaming ^
  --folder-target config:%ProgramData%
```

### Step 2: Create Configuration File

Create a `packager.json` file in your input directory with equivalent settings.

**Equivalent configuration file**:
```json
{
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
    }
  ]
}
```

### Step 3: Update Your Build Scripts

Update your build scripts to use the simplified command line.

**Old script**:
```bash
packager.exe C:\MyProject\dist C:\Output\installer.exe ^
  --compression zstd ^
  --app-name MyApplication ^
  --install-dir "%ProgramFiles%" ^
  --folder-target app:installDirectory ^
  --folder-target plugins:%AppData%\Roaming
```

**New script**:
```bash
packager.exe C:\MyProject\dist C:\Output\installer.exe
```

### Step 4: Test Your Configuration

Run the packager with your new configuration and verify:

1. Configuration file is found and loaded
2. All folders are packaged correctly
3. Installer installs files to correct locations
4. Application name is appended correctly

## Command-Line Argument Mapping

### Basic Arguments

| Old Argument | New Configuration Field | Notes |
|--------------|------------------------|-------|
| `--app-name <name>` | `applicationName` | Required in config file |
| `--install-dir <dir>` | `defaultInstallDirectory` | Default: `%ProgramFiles%` |
| `--compression <algo>` | `compressionAlgorithm` | Values: "zstd" or "lzma" |

### Folder Target Arguments

| Old Argument | New Configuration |
|--------------|-------------------|
| `--folder-target <folder>:installDirectory` | `folderTargets` array with `targetDirectory: "installDirectory"` |
| `--folder-target <folder>:%AppData%` | `folderTargets` array with `targetDirectory: "%AppData%\\Roaming"` |
| `--folder-target <folder>:%ProgramData%` | `folderTargets` array with `targetDirectory: "%ProgramData%"` |

## Migration Examples

### Example 1: Simple Application

**Old Command**:
```bash
packager.exe C:\MyApp\build C:\Output\setup.exe --app-name MyApp
```

**New Configuration** (`C:\MyApp\build\packager.json`):
```json
{
  "applicationName": "MyApp"
}
```

**New Command**:
```bash
packager.exe C:\MyApp\build C:\Output\setup.exe
```

### Example 2: Custom Compression

**Old Command**:
```bash
packager.exe C:\MyApp\build C:\Output\setup.exe ^
  --app-name MyApp ^
  --compression lzma
```

**New Configuration**:
```json
{
  "applicationName": "MyApp",
  "compressionAlgorithm": "lzma"
}
```

**New Command**:
```bash
packager.exe C:\MyApp\build C:\Output\setup.exe
```

### Example 3: Multiple Folder Targets

**Old Command**:
```bash
packager.exe C:\MyApp\build C:\Output\setup.exe ^
  --app-name MyApp ^
  --folder-target bin:installDirectory ^
  --folder-target data:%AppData%\Roaming ^
  --folder-target shared:%ProgramData%
```

**New Configuration**:
```json
{
  "applicationName": "MyApp",
  "folderTargets": [
    {
      "folder": "bin",
      "targetDirectory": "installDirectory"
    },
    {
      "folder": "data",
      "targetDirectory": "%AppData%\\Roaming"
    },
    {
      "folder": "shared",
      "targetDirectory": "%ProgramData%"
    }
  ]
}
```

**New Command**:
```bash
packager.exe C:\MyApp\build C:\Output\setup.exe
```

### Example 4: Custom Install Directory

**Old Command**:
```bash
packager.exe C:\MyApp\build C:\Output\setup.exe ^
  --app-name MyApp ^
  --install-dir "%ProgramFiles(x86)%"
```

**New Configuration**:
```json
{
  "applicationName": "MyApp",
  "defaultInstallDirectory": "%ProgramFiles(x86)%"
}
```

**New Command**:
```bash
packager.exe C:\MyApp\build C:\Output\setup.exe
```

## Backward Compatibility

### Installer Compatibility

**Good News**: Installers generated with the new packager are **fully compatible** with older systems.

- Old installers can still be run on any system
- New installers work the same way as old installers
- Metadata format is backward compatible

### Metadata Compatibility

The new metadata format extends the old format:

**Old Metadata** (still supported):
```json
{
  "version": "1.0",
  "folders": [
    {
      "name": "app",
      "offset": 0,
      "size": 1024
    }
  ]
}
```

**New Metadata** (with extensions):
```json
{
  "version": "1.0",
  "applicationName": "MyApp",
  "defaultInstallDirectory": "%ProgramFiles%",
  "folders": [
    {
      "name": "app",
      "offset": 0,
      "size": 1024,
      "targetDirType": "installDirectory"
    }
  ]
}
```

**Compatibility Rules**:
- Old installers can read old metadata ✓
- New installers can read old metadata ✓ (uses defaults)
- New installers can read new metadata ✓
- Old installers ignore new fields ✓ (graceful degradation)

### Configuration File Optional

**Important**: Configuration files are **optional**. If no configuration file is found:

- Packager uses default values
- All folders install to user-selected directory
- ZSTD compression is used
- Application name defaults to "MyApplication"

This ensures the packager still works without a configuration file.

## Environment Variable Configuration

You can override the configuration file location using an environment variable:

```bash
set PACKAGER_CONFIG=C:\MyConfigs\custom.json
packager.exe C:\MyApp\build C:\Output\setup.exe
```

This is useful for:
- CI/CD pipelines with centralized configurations
- Testing different configurations
- Shared configurations across projects

## Build Script Integration

### Batch Script Example

**Old Script** (`build.bat`):
```batch
@echo off
set APP_NAME=MyApplication
set INPUT_DIR=C:\MyProject\dist
set OUTPUT_FILE=C:\Output\installer.exe

packager.exe %INPUT_DIR% %OUTPUT_FILE% ^
  --app-name %APP_NAME% ^
  --compression zstd ^
  --folder-target app:installDirectory ^
  --folder-target plugins:%%AppData%%\Roaming
```

**New Script** (`build.bat`):
```batch
@echo off
set INPUT_DIR=C:\MyProject\dist
set OUTPUT_FILE=C:\Output\installer.exe

REM Configuration is in %INPUT_DIR%\packager.json
packager.exe %INPUT_DIR% %OUTPUT_FILE%
```

### PowerShell Script Example

**Old Script** (`build.ps1`):
```powershell
$appName = "MyApplication"
$inputDir = "C:\MyProject\dist"
$outputFile = "C:\Output\installer.exe"

& packager.exe $inputDir $outputFile `
  --app-name $appName `
  --compression zstd `
  --folder-target "app:installDirectory" `
  --folder-target "plugins:%AppData%\Roaming"
```

**New Script** (`build.ps1`):
```powershell
$inputDir = "C:\MyProject\dist"
$outputFile = "C:\Output\installer.exe"

# Configuration is in $inputDir\packager.json
& packager.exe $inputDir $outputFile
```

### CMake Integration Example

**Old CMakeLists.txt**:
```cmake
add_custom_command(
  OUTPUT installer.exe
  COMMAND packager.exe ${CMAKE_BINARY_DIR}/dist installer.exe
    --app-name MyApp
    --compression zstd
  DEPENDS dist_target
)
```

**New CMakeLists.txt**:
```cmake
# Copy configuration file to dist directory
configure_file(
  ${CMAKE_SOURCE_DIR}/packager.json
  ${CMAKE_BINARY_DIR}/dist/packager.json
  COPYONLY
)

add_custom_command(
  OUTPUT installer.exe
  COMMAND packager.exe ${CMAKE_BINARY_DIR}/dist installer.exe
  DEPENDS dist_target packager.json
)
```

## Troubleshooting Migration Issues

### Issue: "Missing required field: applicationName"

**Cause**: Configuration file doesn't specify `applicationName`

**Solution**: Add `applicationName` to your configuration:
```json
{
  "applicationName": "YourAppName"
}
```

### Issue: "Configuration file not found"

**Cause**: Configuration file is not in the input directory or has wrong name

**Solutions**:
1. Verify file is named `packager.json` or `.packager.json`
2. Verify file is in the input directory root
3. Use `PACKAGER_CONFIG` environment variable to specify path

### Issue: "Folder does not exist in input directory"

**Cause**: Folder name in configuration doesn't match actual folder

**Solution**: Verify folder names match exactly:
```json
{
  "folderTargets": [
    {
      "folder": "app",  // Must match actual folder name
      "targetDirectory": "installDirectory"
    }
  ]
}
```

### Issue: Installer puts files in wrong location

**Cause**: Target directory configuration may be incorrect

**Solution**: Review target directory values:
- Use `"installDirectory"` for user-selected directory
- Use `"%AppData%\\Roaming"` for user AppData (note double backslash)
- Use `"%ProgramData%"` for shared data

### Issue: Application name duplicated in path

**Cause**: This should not happen - the installer automatically detects if app name is already present

**Solution**: If this occurs, please report as a bug. The installer should check if the path already contains the application name.

## Best Practices for Migration

1. **Migrate One Project at a Time**: Don't try to migrate all projects simultaneously

2. **Keep Old Scripts Temporarily**: Keep old build scripts as backup during migration

3. **Test Thoroughly**: Test installation on clean systems after migration

4. **Version Control Configuration**: Add `packager.json` to your repository

5. **Document Custom Configurations**: Add comments in a README if your configuration is complex

6. **Use Environment Variables**: For system paths, prefer `%ProgramFiles%` over hardcoded paths

7. **Validate Before Committing**: Run packager with new configuration before committing changes

## Getting Help

If you encounter issues during migration:

1. Check the [Configuration Reference](configuration_reference.md) for detailed option documentation
2. Review [Examples](../examples/configurations/) for common configuration patterns
3. Enable verbose logging to see configuration loading details
4. Verify JSON syntax using a JSON validator

## Summary

Migration to configuration files:
- ✓ Simplifies command-line interface
- ✓ Improves configuration management
- ✓ Maintains backward compatibility
- ✓ Enables better version control
- ✓ Provides better validation and error messages

The migration process is straightforward:
1. Create `packager.json` in your input directory
2. Move command-line arguments to configuration file
3. Simplify your build scripts
4. Test and verify

Configuration files are optional - the packager still works without them using default values.
