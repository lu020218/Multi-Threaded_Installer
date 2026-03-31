# Test Cases

## Scope

This checklist covers:
- packager schema v2 loading
- folder payload packaging
- GUI install
- silent install
- uninstall
- upgrade cleanup
- component install
- shortcut and startup behavior
- payload extraction stability

## Test Environment

- Windows 10 22H2 and Windows 11 23H2 or later
- x64
- DPI 100%, 150%, 200%
- standard user install path with elevation
- administrator-run install path

## Preparation

### Build Outputs

Ensure these files exist next to `packager.exe`:
- `installer.exe`
- `uninstaller.exe`
- `resources/`

### Config Preparation

Prepare a schema v2 `packager.yaml` covering:
- `app`
- `package`
- `install`
- `ui`
- `layout`
- `lifecycle`

Recommended coverage:
- `lifecycle.compatibility.legacyAppIds`
- `ui.desktopShortcut.defaultName`
- `ui.desktopShortcut.i18n`
- `lifecycle.compatibility.legacyDesktopShortcutNames`
- `lifecycle.cleanup.onUpgrade.registry.deleteFromManifest`
- `lifecycle.cleanup.onUpgrade.registry.legacyKeys`
- `lifecycle.cleanup.onUpgrade.extraPaths`
- `lifecycle.registry.onInstall`
- component definitions

## Packager Tests

### PKG-001 Valid schema v2 config

Steps:
1. Run `packager.exe` with a valid `packager.yaml`

Expected:
- config loads successfully
- installer is generated successfully

### PKG-002 Missing schemaVersion

Steps:
1. Remove `schemaVersion`
2. Run `packager.exe`

Expected:
- fails
- error mentions `schemaVersion`

### PKG-003 Wrong schemaVersion

Steps:
1. Set the schema version to an unsupported value such as `1`
2. Run `packager.exe`

Expected:
- fails
- error says schema version 2 is required

### PKG-004 Invalid compression algorithm

Steps:
1. Set `package.compression.algorithm: lzma`
2. Run `packager.exe`

Expected:
- fails
- error points to `package.compression.algorithm`

### PKG-005 Invalid destination type

Steps:
1. Set `layout.folders[0].destination.type: unknown`
2. Run `packager.exe`

Expected:
- fails
- error points to `layout.folders[].destination.type`

### PKG-006 Unknown component folder reference

Steps:
1. Set `layout.components[0].folders` to an undeclared folder id
2. Run `packager.exe`

Expected:
- fails
- error points to `layout.components[].folders`

### PKG-007 Single folder package

Steps:
1. Provide one top-level folder
2. Run `packager.exe`

Expected:
- package succeeds
- installer metadata contains one folder payload

### PKG-008 Multiple folder package

Steps:
1. Provide at least two top-level folders
2. Run `packager.exe`

Expected:
- package succeeds
- each top-level folder becomes one payload

### PKG-009 XZ payload header validation

Steps:
1. Package with `package.compression.algorithm: xz`
2. Inspect payload bytes

Expected:
- payload starts with standard XZ header
- no custom block header exists

### PKG-010 ZSTD payload package

Steps:
1. Package with `package.compression.algorithm: zstd`

Expected:
- package succeeds
- installer can later consume the result

## Install Tests

### INS-GUI-001 Default GUI install

Steps:
1. Launch installer
2. Install using default path

Expected:
- install succeeds
- `install.manifest.json` exists
- `uninstall.exe` exists

### INS-GUI-002 Custom install path

Steps:
1. Launch installer
2. Change install path
3. Install

Expected:
- files are installed to selected path

### INS-GUI-003 Component selection

Steps:
1. Launch installer
2. Select only part of optional components
3. Install

Expected:
- only selected component actions are executed

### INS-SIL-001 Minimal silent install

Steps:
1. Run:

```powershell
installer.exe --silent
```

Expected:
- install succeeds without GUI

### INS-SIL-002 Silent install with destination and components

Steps:
1. Run:

```powershell
installer.exe --silent --destination "D:\Apps\MyApp" --components all
```

Expected:
- install succeeds at selected path
- all optional components are installed

### INS-SIL-003 Silent install startup and desktop icon

Steps:
1. Run:

```powershell
installer.exe --silent --auto-startup true --desktop-icon true
```

Expected:
- startup entry is created
- desktop shortcut is created

## Uninstall Tests

### UNI-GUI-001 GUI uninstall

Steps:
1. Run installed `uninstall.exe`

Expected:
- uninstall succeeds
- install directory cleanup matches manifest and cleanup rules

### UNI-SIL-001 Silent uninstall

Steps:
1. Run:

```powershell
uninstall.exe --silent
```

Expected:
- uninstall succeeds without GUI

## Upgrade Cleanup Tests

### UPG-001 Old install directory cleanup

Steps:
1. Install old version in one directory
2. Install new version in another directory with upgrade cleanup enabled

Expected:
- old install files are removed
- old manifest and uninstall binary are removed

### UPG-002 Legacy startup cleanup

Steps:
1. Install old version with startup enabled
2. Upgrade to new version

Expected:
- old startup entry is removed

### UPG-003 Legacy registry cleanup

Steps:
1. Create old registry entries
2. Upgrade to new version

Expected:
- configured legacy keys are removed

### UPG-004 Extra path cleanup

Steps:
1. Create files in configured `lifecycle.cleanup.onUpgrade.extraPaths`
2. Upgrade to new version

Expected:
- configured extra paths are cleaned according to rules

## Shortcut and Localization Tests

### UI-001 Default shortcut name

Expected:
- desktop shortcut uses `ui.desktopShortcut.defaultName` when no language-specific name exists

### UI-002 Localized shortcut name

Expected:
- desktop shortcut uses language-specific value from `ui.desktopShortcut.i18n`

### UI-003 Legacy shortcut cleanup

Expected:
- upgrade cleanup removes old shortcut names from `lifecycle.compatibility.legacyDesktopShortcutNames`

## Payload Stability Tests

### PAY-001 Large XZ payload

Steps:
1. Package a folder with a large executable, e.g. 177MB+
2. Install

Expected:
- install succeeds
- no crash or unexpected interruption during extraction

### PAY-002 Multiple folder concurrent extraction

Steps:
1. Package at least two large folders
2. Install

Expected:
- folder-level parallel install succeeds
- logs show payload-level extraction behavior

### PAY-003 MT decoder logging

Expected logs for XZ payload:
- decoder decision log
- whether MT decoder was used
- thread count
- fallback reason when MT is not used

## Documentation Consistency Checks

### DOC-001 Schema references

Steps:
1. Search docs and examples

Expected:
- no active examples using schema version 1
- no docs instruct users to use unsupported config filenames
- no docs describe old flat config keys as current schema
