# Componentized Install Troubleshooting Guide

## Scope

This guide covers troubleshooting for componentized install/uninstall in metadata v13:
- UI selection binding (`userdata="component:<id>"`)
- Runtime component execution (`embedded` / `local` / `download`)
- Uninstall replay (`install.manifest.json` -> `componentActions[]`)

## Quick Checks

1. Verify packager config passes validation.
2. Confirm installer metadata version is `13`.
3. Confirm component IDs are unique and dependencies are acyclic.
4. Confirm `local` installers stay under `%InstallDir%`.
5. Confirm `download` uses `https://` and a 64-char SHA256 digest.

## Symptom: Component Not Installed

Possible causes:
- Component is not selected (neither default nor CLI-selected).
- UI binding token does not match component id.
- Dependency resolution excludes component due to invalid id reference.

Checks:
- In GUI mode, verify checkbox `userdata` is `component:<id>`.
- In CLI mode, verify `--component` / `--components` ids are exact matches.
- Check logs for `Unknown selected component id`.

## Symptom: Local Component Fails To Execute

Possible causes:
- Installer path escapes base path.
- Executable/script is missing under install root.
- Process returns non-zero exit code.

Checks:
- `source.local.base` starts with `%InstallDir%` or `installDirectory`.
- `source.local.installer` is relative and does not contain `..`.
- Verify target file exists under install root at runtime.
- Increase `timeoutSec` if command can run long.

## Symptom: Download Component Fails

Possible causes:
- URL is not reachable or not HTTPS.
- Download hash mismatch.
- Target path resolves outside install root.

Checks:
- Confirm URL starts with `https://`.
- Verify SHA256 is correct and lowercase/uppercase-insensitive exact match.
- Confirm `saveAs` resolves under `%InstallDir%`.

## Symptom: Uninstall Did Not Replay Component Cleanup

Possible causes:
- Component install did not record `uninstall` command.
- Manifest did not include `componentActions[]`.
- Replay command failed during uninstall.

Checks:
- Open `install.manifest.json` and inspect `componentActions`.
- Verify each action has `componentId`, `uninstallCommand`, `wait`, `timeoutSec`.
- Check uninstall logs for replay warnings.
- Note: replay failures are warning-level and uninstall continues.

## CLI Reference (Component Selection)

- `--component <id>`: repeatable component id selection
- `--components <id1,id2,...>`: CSV component selection
- `--all-components`: include all optional components

Selection rules:
- No component flags: `required + defaultSelected`
- With explicit ids: `required + explicit + dependency closure`

## Recommended Diagnostic Artifacts

Collect these when reporting an issue:
- Packager config file (`packager.yaml` / `packager.json`)
- Installer console log (or GUI runtime log)
- Generated `install.manifest.json`
- Exact CLI invocation
- Relevant filesystem paths under install root
