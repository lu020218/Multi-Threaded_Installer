# ZSTD Phase 5 Test & Acceptance Report

Date: 2026-02-17

## Scope

Phase 5 implementation for ZSTD/LZMA support:
- config parser/validator tests
- compression/decompression loop tests
- end-to-end packaging/install verification

## New automated tests added

1. `tests/test_config_compression_options.cpp`
- validates `compressionAlgorithm` / `compressionLevel` parse and validation paths
- covers valid JSON/YAML and invalid cases (unknown algorithm, non-integer level, out-of-range level)

2. `tests/test_compression_decompression_roundtrip.cpp`
- validates packager compression + installer decompression roundtrip
- covers LZMA and ZSTD paths

## CMake test registration

Added ctest entries:
- `config_compression_options`
- `compression_decompression_roundtrip`

## Unit/Integration test run result

Command:

```powershell
ctest --test-dir build-phase5 -C Release --output-on-failure
```

Result:
- Passed: 8
- Failed: 0

Included tests:
- `manifest_roundtrip`
- `utf8_utils`
- `console_component_args`
- `uninstall_component_replay`
- `config_yaml_json_equivalence`
- `configuration_validator_components`
- `config_compression_options`
- `compression_decompression_roundtrip`

## End-to-end acceptance result (manual)

Validated both chains with real binaries:
1. `packager -a lzma -l 6` -> installer silent install -> file content verified
2. `packager -a zstd -l 3` -> installer silent install -> file content verified

Acceptance marker:
- `PHASE5_E2E_OK`

## Notes

- Existing regression script `tests/stage5_install_uninstall_regression.ps1` was invoked and failed at case `ChinesePath` with install exit code `1` in this environment.
- This is tracked as a residual issue and appears independent from the new ZSTD pipeline unit/roundtrip/e2e verification above.
