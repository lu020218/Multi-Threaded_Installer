# Stage 5 Regression Tests

## Install/Uninstall Path Regression

`tests/stage5_install_uninstall_regression.ps1` validates silent install and silent uninstall for:
- Chinese destination path
- Destination path with spaces
- Long destination path

### Usage

```powershell
pwsh -File tests/stage5_install_uninstall_regression.ps1 -BuildDir build -Config Release
```

Optional:
- `-KeepArtifacts` to preserve generated installers and temporary input data.
- `-StepTimeoutSec` to adjust per-process timeout.
- `-CleanupWaitSec` to adjust post-uninstall cleanup wait.
## Manifest Round-Trip

`tests/test_manifest_roundtrip.cpp` validates manifest write/read/write round-trip for `src/installer/uninstall_manager.cpp`.

### Build And Run

```powershell
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --config Release --target test_manifest_roundtrip
ctest --test-dir build -C Release -R manifest_roundtrip -V
```

## UTF-8/ACP Conversion Unit Test

`tests/test_utf8_utils.cpp` validates UTF-8 strict decoding, multibyte conversion overload behavior, ACP bridge consistency, and path/TCHAR helper conversion in `src/common/utf8_utils.cpp`.

### Build And Run

```powershell
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --config Release --target test_utf8_utils
ctest --test-dir build -C Release -R utf8_utils -V
```

## Config YAML/JSON Equivalence Test

`tests/test_config_yaml_json_equivalence.cpp` validates:
- semantic equivalence between JSON legacy config and structured YAML config parsing
- config discovery precedence (`packager.yaml` over `packager.json`)

### Build And Run

```powershell
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --config Release --target test_config_yaml_json_equivalence
ctest --test-dir build -C Release -R config_yaml_json_equivalence -V
```

## Component Validator Security Rules Test

`tests/test_configuration_validator_components.cpp` validates:
- local installer path restriction (relative path only)
- local installer parent traversal rejection (`..`)
- download source security checks (`https://` + 64-char SHA256)
- dependency cycle rejection and valid dependency graph acceptance

### Build And Run

```powershell
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --config Release --target test_configuration_validator_components
ctest --test-dir build -C Release -R configuration_validator_components -V
```

## Compression Option Parsing/Validation Test

`tests/test_config_compression_options.cpp` validates:
- `compressionAlgorithm`/`compressionLevel` parse path for JSON/YAML
- unknown algorithm rejection
- non-integer level rejection
- out-of-range level rejection by validator

### Build And Run

```powershell
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --config Release --target test_config_compression_options
ctest --test-dir build -C Release -R config_compression_options -V
```

## Compression/Decompression Roundtrip Test

`tests/test_compression_decompression_roundtrip.cpp` validates:
- packager compression and installer decompression loop
- LZMA roundtrip
- ZSTD roundtrip (when `ZSTD_FOUND` is enabled in build)

### Build And Run

```powershell
cmake -S . -B build -DBUILD_TESTS=ON -DENABLE_ZSTD=ON
cmake --build build --config Release --target test_compression_decompression_roundtrip
ctest --test-dir build -C Release -R compression_decompression_roundtrip -V
```
