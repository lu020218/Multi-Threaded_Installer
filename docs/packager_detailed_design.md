# Packager Detailed Design

## Scope

`packager.exe` generates a self-extracting installer from:
- `--input`: payload source directory.
- `--config`: config and UI resource directory.
- `--output`: generated installer exe.

The only supported public config schema is `schemaVersion: 3`.

## Configuration

The loader accepts `packager.yaml` or `packager.yml` from the config directory.

Top-level v3 blocks:
- `app`
- `package`
- `installer`
- `uninstaller`

The loader rejects older schemas before validation. Validation then checks product identity, payload sources, component references, installState stores, cleanup policy, and elevation-sensitive settings.

## Payload

`installer.payload[]` is the sole payload source:
- `id`: stable payload id.
- `source`: directory under `--input`.
- `target`: install-time destination.
- `required`: whether payload is mandatory.

Components reference payload ids through `installer.components[].payload[]`.

## Manifest

The packager builds a sectioned `PackageManifest` directly from v3 config and compression results. It contains:
- app identity.
- installer policy.
- payload folder metadata and file index.
- component definitions and actions.
- UI metadata.
- uninstaller detect and cleanup policy.

Runtime installation writes a separate local v3 `install.manifest.json` snapshot for uninstall.

## Resources

Sources:
- `packager.yaml`, `resources/`, and relative `app.icon`: config directory.
- installer/uninstaller templates and optional `DuiLib.dll`: packager executable directory.
- payload folders: input directory.

## Output Layout

```text
[installer template + embedded UI resources + embedded uninstaller]
[sectioned package manifest]
[compressed payload blocks]
[DataLocator]
[end magic]
```
