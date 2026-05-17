# Test Cases

## Packager

### PKG-001 Valid v3 config

1. Prepare payload directories matching `installer.payload[].source`.
2. Place `packager.yaml`, `resources/`, and `app.ico` in the config directory.
3. Run `packager.exe --input <payload> --config <config> --output <installer.exe>`.

Expected:
- packager loads `schemaVersion: 3`.
- payload folders are scanned from `installer.payload[]`.
- package manifest contains app, installer, payload, components, installState, and uninstaller policy sections.
- installer exe is generated.

### PKG-002 v2 rejected

1. Run packager with a config using an older schema.

Expected:
- packager fails before packaging.
- error states that only schemaVersion 3 is supported.

### PKG-003 Invalid payload reference

1. Reference an undeclared payload id from `installer.components[].payload[]`.

Expected:
- validation fails and reports the component payload reference.

### PKG-004 installState multi-store

1. Configure multiple `installer.installState.registries[]` and `installer.installState.files[]`.

Expected:
- loader preserves all stores and custom values.
- manifest codec round-trips all stores and values.

### PKG-005 ordered legacy detect

1. Configure `installer.installState.detect.primary` plus multiple `legacy[]`.

Expected:
- loader preserves legacy detect order.
- validator rejects duplicate legacy ids.
- manifest codec round-trips all detect sources.

## Installer

### INS-001 GUI install

1. Launch generated installer without arguments.
2. Complete installation.

Expected:
- default directory and UI defaults come from `installer`.
- selected components affect payload extraction and component install actions.
- installState is written with state `installed`.
- local `install.manifest.json` contains the v3 install/uninstall snapshot.

### INS-002 Silent install

```powershell
installer.exe --silent
```

Expected:
- installation completes without GUI.
- installState and system uninstall entry follow v3 policy.

### INS-003 Overwrite install

1. Install once.
2. Run installer again.

Expected:
- previous install is discovered through primary or legacy `installer.installState.detect` sources.
- old files are cleaned according to the previous manifest.
- missing manifest fallback follows `uninstaller.cleanup.missingManifestFallback`.

### INS-004 Upgrade install

```powershell
installer.exe --upgrade
installer.exe --upgrade --silent
```

Expected:
- upgrade requires a detected previous install directory; if the previous manifest is unreadable, current package defaults are used.
- install directory and previous install options are reused.
- GUI upgrade starts directly on the progress page.

## Uninstaller

### UN-001 Manifest uninstall

1. Run installed `uninstall.exe`.

Expected:
- uninstall reads local `install.manifest.json`.
- installed files are cleaned from the manifest file list.
- installState cleanup follows `delete`, `markUninstalled`, or `keep`.
- actual shortcuts, startup entries, and system uninstall entries are removed from the manifest snapshot.

### UN-002 Safe fallback uninstall

1. Delete local manifest.
2. Keep installState discovery data.
3. Run uninstaller.

Expected:
- fallback only runs when `missingManifestFallback: safeDirectoryFallback`.
- dangerous paths are rejected.
- no background deletion continues after UI completion.

### UN-003 Damaged manifest

1. Corrupt local manifest.
2. Run uninstaller.

Expected:
- uninstall fails explicitly.
- directory-level fallback is not used for a damaged or incomplete manifest.
