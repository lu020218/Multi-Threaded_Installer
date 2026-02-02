# YAML Flow Demo

This example shows the new YAML-only packager configuration plus embedded install flow/script.

## Files

- `packager.yaml` - packager configuration (YAML only)
- `flow.yaml` - embedded install flow DSL
- `scripts/precheck.js` - script step referenced by flow
- `app/app.exe` - demo payload file
- `component_manifest.yaml` - optional component manifest example
- `ui_schema.yaml` - dynamic UI control schema example
- `flow-components.yaml` - optional component install flow example

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
