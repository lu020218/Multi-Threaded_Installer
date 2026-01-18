# Single-File Installer Implementation Guide

## Overview

This document describes how to create a truly single-file installer that embeds all dependencies (DLLs and resources) within the executable.

## Current Implementation Status

### ✅ Phase 1: Runtime Resource Extraction (COMPLETED)

The installer now includes:
- **EmbeddedResourceManager** class to extract resources at runtime
- **Automatic fallback** to external resources if extraction fails
- **Graceful degradation** to console mode if GUI resources unavailable

### 🔄 Phase 2: Resource Embedding (IN PROGRESS)

A PowerShell script (`embed_resources.ps1`) is provided to embed resources into the installer.

### ⏳ Phase 3: Static Linking (PLANNED)

Future work to eliminate DLL dependencies entirely.

## How It Works

### Architecture

```
┌─────────────────────────────────────┐
│     installer.exe (Single File)     │
├─────────────────────────────────────┤
│  Original PE Executable             │
│  - Installer code                   │
│  - Compressed application data      │
├─────────────────────────────────────┤
│  Embedded Resources (Appended)      │
│  - DuiLib.dll                       │
│  - liblzma.dll                      │
│  - XML layouts (main.xml, etc.)     │
│  - Images                           │
│  - license.txt                      │
├─────────────────────────────────────┤
│  Resource Index                     │
│  - Magic number: 0x52534D45         │
└─────────────────────────────────────┘
```

### Runtime Flow

1. **Startup**: Installer.exe launches
2. **Check**: Look for external resources (backward compatibility)
3. **Extract**: If not found, extract embedded resources to temp directory
4. **Run**: Use extracted resources for GUI
5. **Cleanup**: Delete temp resources on exit

## Creating a Single-File Installer

### Step 1: Build the Installer

```cmd
cmake --build build --config Release --target installer
```

This creates `build\Release\installer.exe` with external dependencies.

### Step 2: Embed Resources

Run the embedding script:

```powershell
.\scripts\embed_resources.ps1 -InstallerPath build\Release\installer.exe
```

**Options:**
```powershell
# Custom resource locations
.\scripts\embed_resources.ps1 `
    -InstallerPath build\Release\installer.exe `
    -ResourceDir build\Release\resources `
    -DuiLibDll build\Release\DuiLib.dll `
    -LibLzmaDll build\Release\liblzma.dll
```

### Step 3: Test

```cmd
# Copy to a clean directory
mkdir test_single_file
copy build\Release\installer.exe test_single_file\

# Run from clean directory (no external files)
cd test_single_file
installer.exe
```

**Expected behavior:**
- ✅ Installer extracts resources to temp directory
- ✅ GUI appears normally
- ✅ Installation proceeds
- ✅ Temp resources cleaned up on exit

## Packaging Workflow

### For Packager-Generated Installers

```cmd
# 1. Build installer and packager
cmake --build build --config Release

# 2. Run packager to create installer with data
build\Release\packager.exe input output\MyApp_Setup.exe

# 3. Embed resources into the packaged installer
.\scripts\embed_resources.ps1 -InstallerPath output\MyApp_Setup.exe

# 4. Distribute single file
# output\MyApp_Setup.exe is now completely self-contained!
```

### Automated Build Script

Create `build_single_file.bat`:

```batch
@echo off
echo Building single-file installer...

REM Build
cmake --build build --config Release --target installer
if errorlevel 1 goto error

REM Package
build\Release\packager.exe input output\MyApp_Setup.exe
if errorlevel 1 goto error

REM Embed resources
powershell -ExecutionPolicy Bypass -File scripts\embed_resources.ps1 -InstallerPath output\MyApp_Setup.exe
if errorlevel 1 goto error

echo.
echo SUCCESS! Single-file installer created: output\MyApp_Setup.exe
goto end

:error
echo.
echo ERROR: Build failed
exit /b 1

:end
```

## Resource Extraction Details

### Temporary Directory

Resources are extracted to:
```
%TEMP%\MTInstaller_{ProcessID}_{TickCount}\
├── DuiLib.dll
├── liblzma.dll
├── skins\
│   ├── main.xml
│   ├── welcome_page.xml
│   ├── progress_page.xml
│   ├── completion_page.xml
│   └── license.xml
├── images\
└── license.txt
```

### Cleanup

- **Normal exit**: Resources deleted automatically
- **Crash**: Temp files may remain (Windows will clean eventually)
- **Manual cleanup**: Delete `%TEMP%\MTInstaller_*` directories

## Advantages

### ✅ Single-File Distribution
- One .exe file to distribute
- No ZIP archives needed
- Simpler for end users

### ✅ Backward Compatible
- Still works with external resources
- Fallback to console mode if needed
- No breaking changes

### ✅ Flexible
- Can update resources without recompiling
- Easy to customize per-deployment

## Limitations

### Current Limitations

1. **DLL Dependencies**
   - DuiLib.dll still required (extracted to temp)
   - liblzma.dll still required (extracted to temp)
   - Size: ~1.8 MB additional

2. **Temp Directory**
   - Requires write access to %TEMP%
   - Disk I/O on every run
   - Potential antivirus flags

3. **File Size**
   - Embedded resources increase .exe size
   - Typical overhead: 2-3 MB

### Future Improvements

**Phase 2: Static Linking**
- Rebuild DuiLib as static library
- Eliminates DuiLib.dll dependency
- Reduces size and complexity

**Phase 3: Resource Compilation**
- Use Windows .rc files
- Embed resources in PE format
- No extraction needed (load from memory)

## Troubleshooting

### Resources Not Extracting

**Symptom:** Installer shows "GUI资源文件未找到" error

**Causes:**
1. Resources not embedded (forgot to run embed_resources.ps1)
2. Temp directory not writable
3. Antivirus blocking extraction

**Solutions:**
1. Verify resources embedded:
   ```powershell
   # Check file size - should be larger after embedding
   (Get-Item installer.exe).Length / 1MB
   ```

2. Check temp directory:
   ```cmd
   echo %TEMP%
   dir %TEMP%\MTInstaller_*
   ```

3. Run as administrator or whitelist in antivirus

### Antivirus False Positives

**Issue:** Antivirus flags installer as malware

**Reason:** Self-extracting executables are sometimes flagged

**Solutions:**
1. **Code signing**: Sign the installer with a certificate
2. **Whitelist**: Submit to antivirus vendors
3. **Alternative**: Use external resources (no extraction)

### Large File Size

**Issue:** Installer.exe is very large

**Analysis:**
```
Base installer:     ~450 KB
DuiLib.dll:        ~1.6 MB
liblzma.dll:       ~185 KB
Resources:         ~100 KB
Application data:  Variable
─────────────────────────────
Total:             2.3 MB + data
```

**Solutions:**
1. **Compress resources**: Use UPX or similar
2. **Minimize resources**: Remove unused images
3. **Static linking**: Eliminate DLL overhead

## Testing Checklist

### Before Release

- [ ] Build installer with Release configuration
- [ ] Run embed_resources.ps1 successfully
- [ ] Test in clean directory (no external files)
- [ ] Verify GUI appears correctly
- [ ] Test installation completes
- [ ] Check temp directory cleanup
- [ ] Test on different Windows versions
- [ ] Scan with antivirus
- [ ] Verify file size is reasonable

### Test Scenarios

1. **Clean Install**
   ```cmd
   mkdir test_clean
   copy installer.exe test_clean\
   cd test_clean
   installer.exe
   ```

2. **With External Resources** (backward compat)
   ```cmd
   mkdir test_external
   copy installer.exe test_external\
   xcopy /E resources test_external\resources\
   copy DuiLib.dll test_external\
   cd test_external
   installer.exe
   ```

3. **Console Fallback**
   ```cmd
   # Remove embedded resources (for testing)
   installer.exe -s
   ```

## Performance Considerations

### Extraction Time

- **DuiLib.dll**: ~50ms
- **liblzma.dll**: ~10ms
- **XML files**: ~5ms each
- **Total**: ~100-150ms overhead

### Disk Space

- **Temp usage**: ~2 MB during installation
- **Cleanup**: Automatic on exit

### Memory

- **Extraction**: Minimal (streaming)
- **Runtime**: Same as external resources

## Security Considerations

### Code Signing

**Highly Recommended** for production:

```cmd
signtool sign /f certificate.pfx /p password /t http://timestamp.server installer.exe
```

Benefits:
- Reduces antivirus false positives
- Builds user trust
- Required for some enterprise environments

### Integrity

The embedded resources are not encrypted or signed separately. Consider:

1. **Checksum verification**: Add checksums to resource index
2. **Encryption**: Encrypt sensitive resources
3. **Tamper detection**: Verify PE signature

## Summary

The single-file installer implementation provides:

✅ **Convenience**: One file to distribute
✅ **Compatibility**: Works with or without embedded resources
✅ **Flexibility**: Easy to customize and update

**Current Status**: Functional with runtime extraction
**Next Steps**: Static linking to eliminate DLL dependencies

**Recommendation**: Use for production deployments where single-file distribution is important.
