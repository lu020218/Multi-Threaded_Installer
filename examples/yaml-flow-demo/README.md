# YAML Flow Demo

This example shows the new YAML-only packager configuration plus embedded install flow/script.

## Files

- `packager.yaml` - packager configuration (YAML only)
- `flow.yaml` - embedded install flow DSL
- `scripts/precheck.js` - script step referenced by flow
- `app/app.exe` - demo payload file
- `component_manifest.yaml` - optional component manifest example
- `ui_schema.yaml` - dynamic UI control schema example
- `flow-components.yaml` - optional component batch flow (`resolve_selected_components` + `process_selected_components`)

## Build Demo Package

From workspace root:

```bash
cargo run -p packager_cli -- \
  --input ./examples/yaml-flow-demo \
  --output ./examples/yaml-flow-demo/demo.pkg \
  --package-only
```

## Install With Embedded Flow

```bash
cargo run -p installer_cli -- \
  --package ./examples/yaml-flow-demo/demo.pkg \
  --install-dir C:\\Temp\\YamlFlowDemo \
  --silent \
  --enable-scripts \
  --script-allow-root C:\\Temp
```

Note: script execution is disabled by default and requires explicit enable + allowlist.

## Component Batch Flow Notes

`flow-components.yaml` demonstrates the recommended batch pattern:

1. `resolve_selected_components` collects user selections from `options.components.*`
2. `process_selected_components` with `action: download`
3. `process_selected_components` with `action: verify`
4. `process_selected_components` with `action: install`

This avoids repeating per-component `download/verify/install` steps in YAML.
