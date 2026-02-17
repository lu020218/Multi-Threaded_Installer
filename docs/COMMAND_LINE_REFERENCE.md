# Command Line Reference

## Overview

The installer supports command-line parameters for automated deployment scenarios and advanced configuration options.

## Basic Syntax

```cmd
installer.exe [OPTIONS]
```

## Command-Line Options

### Silent Mode

**Parameter**: `-s` or `--silent`

**Description**: Runs the installer in silent mode without displaying the GUI.

**Usage**:
```cmd
installer.exe -s
```

**Behavior**:
- No graphical interface is displayed
- Installation proceeds automatically with default settings
- Progress and status messages are written to console (stdout)
- Exit code indicates success or failure

**Exit Codes**:
- `0`: Installation completed successfully
- `1`: General installation failure
- `2`: Insufficient disk space
- `3`: Permission denied
- `4`: Invalid installation path
- `5`: User cancellation (not applicable in silent mode)
- `6`: Corrupted installer data

**Example Output**:
```
Installer Version 1.0.0
Installing to: C:\Program Files\MyApp
Extracting files...
Progress: 25%
Progress: 50%
Progress: 75%
Progress: 100%
Installation completed successfully.
```

### Installation Path (Future Enhancement)

**Note**: Currently, the installation path must be configured in the installer package. Future versions may support:

```cmd
installer.exe -s --path "C:\Custom\Path"
```

### Help

**Parameter**: `-h`, `--help`, or `/?`

**Description**: Displays help information about available command-line options.

**Usage**:
```cmd
installer.exe --help
```

**Output**:
```
Installer Application v1.0.0

Usage: installer.exe [OPTIONS]

Options:
  -s, --silent    Run in silent mode (no GUI)
  -h, --help      Display this help message
  -v, --version   Display version information

Examples:
  installer.exe              Launch GUI installer
  installer.exe -s           Silent installation
  installer.exe --help       Show this help

Exit Codes:
  0 - Success
  1 - General failure
  2 - Insufficient disk space
  3 - Permission denied
  4 - Invalid path
  5 - User cancellation
  6 - Corrupted data

For more information, visit: [product website]
```

### Version Information

**Parameter**: `-v` or `--version`

**Description**: Displays version information and exits.

**Usage**:
```cmd
installer.exe --version
```

**Output**:
```
Installer Application
Version: 1.0.0
Build Date: 2024-01-15
Copyright (c) 2024
```

## Deployment Scenarios

### Automated Deployment with Batch Script

Create a batch file for automated deployment:

```batch
@echo off
echo Starting automated installation...

REM Run installer in silent mode
installer.exe -s

REM Check exit code
if %ERRORLEVEL% EQU 0 (
    echo Installation completed successfully.
    exit /b 0
) else (
    echo Installation failed with error code %ERRORLEVEL%.
    exit /b %ERRORLEVEL%
)
```

### PowerShell Deployment Script

```powershell
# Run installer and capture exit code
$process = Start-Process -FilePath "installer.exe" -ArgumentList "-s" -Wait -PassThru

# Check result
if ($process.ExitCode -eq 0) {
    Write-Host "Installation completed successfully." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Installation failed with error code: $($process.ExitCode)" -ForegroundColor Red
    exit $process.ExitCode
}
```

### Group Policy Deployment

For enterprise deployment via Group Policy:

1. **Create GPO**:
   - Open Group Policy Management Console
   - Create new GPO or edit existing one

2. **Configure Software Installation**:
   - Navigate to: Computer Configuration → Policies → Software Settings → Software Installation
   - Right-click → New → Package
   - Browse to installer location on network share

3. **Deployment Options**:
   - Assigned: Installs automatically at startup/login
   - Published: Available in Add/Remove Programs

4. **Advanced Options**:
   - Set deployment to "Uninstall this application when it falls out of scope"
   - Configure installation UI level to "Basic" or "None" for silent deployment

### SCCM/ConfigMgr Deployment

For Microsoft System Center Configuration Manager:

**Program Configuration**:
- **Program Name**: Install MyApp
- **Command Line**: `installer.exe -s`
- **Run**: Hidden
- **Program can run**: Whether or not a user is logged on
- **Run mode**: Run with administrative rights

**Detection Method**:
- Check for file existence in installation directory
- Or check registry key: `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{AppGUID}`

### Intune Deployment

For Microsoft Intune:

1. **Package as Win32 App**:
   - Use Microsoft Win32 Content Prep Tool
   - Create .intunewin package from installer

2. **App Information**:
   - Install command: `installer.exe -s`
   - Uninstall command: `uninstall.exe -s`

3. **Detection Rules**:
   - File exists: `C:\Program Files\MyApp\app.exe`
   - Or registry key exists

4. **Requirements**:
   - OS: Windows 10 1607+
   - Architecture: x64
   - Disk space: 100 MB

## Logging

### Console Output (Silent Mode)

In silent mode, the installer writes progress information to stdout:

```cmd
installer.exe -s > install.log 2>&1
```

This captures both standard output and errors to `install.log`.

### Windows Event Log

The installer may write events to Windows Event Log:

**Location**: Applications and Services Logs → Application

**Event IDs**:
- 1000: Installation started
- 1001: Installation completed successfully
- 1002: Installation failed
- 1003: Installation cancelled

**View Events**:
```cmd
eventvwr.msc
```

Or via PowerShell:
```powershell
Get-EventLog -LogName Application -Source "InstallerApp" -Newest 10
```

## Environment Variables

The installer respects the following environment variables:

### TEMP / TMP

**Description**: Temporary directory for extraction.

**Default**: `%USERPROFILE%\AppData\Local\Temp`

**Usage**:
```cmd
set TEMP=D:\Temp
installer.exe -s
```

### ProgramFiles

**Description**: Default installation base directory.

**Default**: `C:\Program Files` (64-bit) or `C:\Program Files (x86)` (32-bit)

## Return Codes Reference

| Code | Meaning | Description |
|------|---------|-------------|
| 0 | Success | Installation completed without errors |
| 1 | General Failure | Unspecified error occurred |
| 2 | Insufficient Space | Not enough disk space at target location |
| 3 | Access Denied | Insufficient permissions for installation |
| 4 | Invalid Path | Installation path is invalid or inaccessible |
| 5 | User Cancelled | User cancelled installation (GUI mode only) |
| 6 | Corrupted Data | Installer package is corrupted |
| 7 | Already Installed | Application is already installed |
| 8 | Incompatible OS | Operating system version not supported |
| 9 | Missing Dependencies | Required dependencies not found |
| 10 | Network Error | Network-related error (if applicable) |

## Best Practices

### 1. Test Before Deployment

Always test silent installation on a representative system:

```cmd
installer.exe -s
echo Exit Code: %ERRORLEVEL%
```

### 2. Check Prerequisites

Verify system meets requirements before running installer:

```batch
@echo off
REM Check Windows version
ver | findstr /i "10.0" > nul
if %ERRORLEVEL% NEQ 0 (
    echo Windows 10 or later required.
    exit /b 8
)

REM Check available disk space (example for C: drive)
for /f "tokens=3" %%a in ('dir C:\ ^| findstr /C:"bytes free"') do set FREE_SPACE=%%a
if %FREE_SPACE% LSS 104857600 (
    echo Insufficient disk space.
    exit /b 2
)

REM Run installer
installer.exe -s
```

### 3. Handle Errors Gracefully

```batch
@echo off
installer.exe -s

if %ERRORLEVEL% EQU 0 goto SUCCESS
if %ERRORLEVEL% EQU 2 goto NO_SPACE
if %ERRORLEVEL% EQU 3 goto NO_PERMISSION
goto GENERAL_ERROR

:SUCCESS
echo Installation successful.
exit /b 0

:NO_SPACE
echo Error: Insufficient disk space.
REM Add remediation logic here
exit /b 2

:NO_PERMISSION
echo Error: Administrator privileges required.
REM Attempt to elevate or notify user
exit /b 3

:GENERAL_ERROR
echo Error: Installation failed with code %ERRORLEVEL%.
exit /b %ERRORLEVEL%
```

### 4. Log All Operations

```cmd
installer.exe -s > "%TEMP%\install_%DATE%_%TIME%.log" 2>&1
```

### 5. Verify Installation

After silent installation, verify success:

```batch
if exist "C:\Program Files\MyApp\app.exe" (
    echo Installation verified.
) else (
    echo Installation verification failed.
    exit /b 1
)
```

## Troubleshooting Command-Line Issues

### Silent Mode Not Working

**Symptom**: GUI appears even with `-s` flag.

**Solutions**:
- Verify parameter syntax: `installer.exe -s` (not `/s`)
- Check for typos in parameter
- Ensure no conflicting parameters

### Exit Code Always 0

**Symptom**: Installer returns 0 even on failure.

**Solutions**:
- Check installer version (older versions may not set exit codes correctly)
- Review console output for actual error messages
- Check Windows Event Log for detailed error information

### Permission Errors in Silent Mode

**Symptom**: Exit code 3 (Access Denied).

**Solutions**:
- Run command prompt as Administrator
- Use `runas` command:
  ```cmd
  runas /user:Administrator "installer.exe -s"
  ```
- Or in PowerShell:
  ```powershell
  Start-Process -FilePath "installer.exe" -ArgumentList "-s" -Verb RunAs
  ```

## Future Enhancements

Planned command-line features for future versions:

- `--path <directory>`: Specify custom installation path
- `--no-shortcuts`: Skip desktop/start menu shortcuts
- `--no-registry`: Skip registry entries (portable mode)
- `--language <code>`: Set installation language
- `--config <file>`: Load configuration from file
- `--extract-only`: Extract files without installing
- `--repair`: Repair existing installation
- `--uninstall`: Uninstall application

## Support

For additional help with command-line deployment:
- Review the User Guide (USER_GUIDE.md)
- Check the Troubleshooting Guide (TROUBLESHOOTING.md)
- Contact technical support with deployment logs

## Component Selection (Current)

Installer now supports explicit component selection in CLI mode:

- `--component <id>`: select one component id (repeatable)
- `--components <id1,id2,...>`: select multiple component ids
- `--all-components`: install all optional components

Selection behavior:

- No component flags: install `required + defaultSelected`
- Explicit selected ids: install `required + selected + dependency closure`
- Invalid component id: installation fails with a clear error message

## Component Uninstall Replay (Current)

When componentized install is used, installer writes component uninstall replay entries to install manifest.
During uninstall, these replay commands are executed before file cleanup.

Current behavior:

- Replay source: `install.manifest.json` -> `componentActions[]`
- Execution: `cmd.exe /c <uninstallCommand>`
- Supports per-action `wait` and `timeoutSec`
- Replay failures are logged as warnings and uninstall continues
- Old manifests without `componentActions` remain supported
