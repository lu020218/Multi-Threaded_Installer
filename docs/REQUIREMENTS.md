# Requirements

## Tools

- `packager.exe`
- generated `installer.exe`
- generated `uninstaller.exe`

## Configuration

- Packager reads `packager.yaml` or `packager.yml` only from the `--config` directory.
- The only supported configuration schema is `schemaVersion: 3`.
- Older schemas are intentionally incompatible and must fail during packager configuration loading.

Minimal structure:

```yaml
schemaVersion: 3
app:
package:
installer:
uninstaller:
```

Required configuration:
- `app.id`
- `app.name`
- `app.version`
- `installer.defaultDir`
- `installer.directoryName`
- `installer.payload[]`
- `installer.installState.detect.primary` or `installer.installState.detect.legacy[]`
- `uninstaller.cleanup`

## Packager CLI

Supported public arguments:
- `--input` / `-i`: payload source directory
- `--config` / `-c`: configuration and UI resource directory
- `--output` / `-o`: generated installer executable
- `--help` / `-h`

No positional paths or legacy compression CLI options are supported.

## Validation

- `installer.payload[].id` must be unique.
- `installer.payload[].source` must exist under the input directory.
- `installer.payload[].target` is the install-time destination.
- `installer.components[].payload[]` must reference declared payload ids.
- component dependencies must not contain cycles.
- `installer.installState.registries[]` and `installer.installState.files[]` support multiple stores and custom values.
- `installer.installState.detect` supports one primary source plus ordered legacy sources for old-version discovery.
- `uninstaller.detect` is not supported; packager rejects it and reports to use `installer.installState.detect`.
- `installer.requireAdmin=false` rejects configurations that clearly require elevation.

## Runtime

- The packager writes the installer execution level from `installer.requireAdmin`.
- The installer records a v3 `install.manifest.json` snapshot after installation.
- Overwrite and upgrade discovery use `installer.installState.detect`: primary first, then legacy entries in order.
- Uninstall uses the local v3 manifest snapshot first and only uses safe fallback when the manifest is missing and policy allows it.

## Acceptance

- A valid v3 config can be packaged into an installer.
- A v2 config is rejected with an explicit unsupported schema error.
- GUI install, silent install, overwrite/upgrade install, manifest uninstall, and safe fallback uninstall are covered by regression tests.
