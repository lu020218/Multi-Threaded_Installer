# Detailed Design

## Configuration Model

The public configuration schema is `schemaVersion: 3`.

Top-level blocks:
- `app`: product identity, icon, version resource metadata.
- `package`: compression behavior.
- `installer`: install policy, UI, payload, components, registry writes, system uninstall entry, and installState stores.
- `uninstaller`: install discovery, cleanup policy, process shutdown, UI, and component uninstall actions.

Older configuration schemas are not compatible and fail during packager loading.

## Packager Flow

1. Parse named CLI arguments: `--input`, `--config`, `--output`.
2. Load `packager.yaml` or `packager.yml` from the config directory.
3. Validate v3 fields and reject invalid references or elevation conflicts.
4. Scan payload directories from `installer.payload[]`.
5. Compress each payload folder.
6. Build a sectioned `PackageManifest` from v3 config and compression results.
7. Write installer execution level from `installer.requireAdmin`.
8. Embed UI resources from the config directory and append metadata plus payload blocks to the installer template.

## Manifest Flow

The package manifest stores:
- identity from `app`.
- install policy from `installer`.
- payload file indexes and compression metadata.
- component definitions and actions.
- UI policy.
- installState detect policy and uninstaller cleanup policy.

The installed `install.manifest.json` is a local v3 snapshot of actual install results. It records released files, selected components, installState stores, actual shortcuts/startup/system uninstall entries, and uninstall policy.

## Install Flow

Fresh install uses `installer.defaultDir` unless overridden by CLI.

Overwrite and upgrade discovery use `installer.installState.detect`: primary source first, then ordered `legacy[]`. Upgrade mode only requires a detected previous install directory; when the previous manifest is unreadable, the current package defaults are used.

Install writes:
- payload files from `installer.payload[]`.
- component install actions from selected `installer.components[]`.
- registry entries from `installer.registry.write`.
- installState stores from `installer.installState`.
- system uninstall entry from `installer.systemUninstallEntry`.
- local v3 install manifest snapshot.

## Uninstall Flow

Uninstall prefers the local v3 manifest snapshot. When the manifest is readable, uninstall behavior is driven by the snapshot, not by the current installer metadata.

Manifest uninstall handles:
- `uninstaller.killBeforeUninstall`.
- manifest file list cleanup.
- `uninstaller.cleanup.installState`.
- actual shortcut/startup/system uninstall entry cleanup.
- registry and extra path cleanup.
- component uninstall actions.

If the manifest is missing, fallback can use configured `installer.installState.detect` sources only when `uninstaller.cleanup.missingManifestFallback` allows `safeDirectoryFallback`. Damaged or incomplete manifests fail instead of guessing.
