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
