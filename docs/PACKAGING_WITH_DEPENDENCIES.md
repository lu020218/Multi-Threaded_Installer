# Packaging Installer with Dependencies - Quick Guide

## Problem

When you run the packager directly:
```cmd
build\Release\packager.exe input output\MyApp_Setup.exe
```

The generated installer is missing:
- ❌ DuiLib.dll
- ❌ liblzma.dll  
- ❌ resources/ directory

Result: "加载资源文件失败：main.xml" error

## Solution

Use the provided packaging script that automatically copies all dependencies.

## Quick Start

### Option 1: Batch Script (Windows CMD)

```cmd
scripts\package_with_dependencies.bat input_directory output\MyApp_Setup.exe
```

**Example:**
```cmd
scripts\package_with_dependencies.bat build\Release\input output\MyApp_Setup.exe
```

### Option 2: PowerShell Script

```powershell
.\scripts\package_with_dependencies.ps1 -InputDir "input_directory" -OutputFile "output\MyApp_Setup.exe"
```

**Example:**
```powershell
.\scripts\package_with_dependencies.ps1 -InputDir "build\Release\input" -OutputFile "output\MyApp_Setup.exe"
```

## What It Does

The script performs two steps:

### Step 1: Run Packager
```cmd
build\Release\packager.exe input output\MyApp_Setup.exe
```

Creates the installer with embedded application data.

### Step 2: Copy Dependencies
```cmd
copy build\Release\DuiLib.dll output\
copy build\Release\liblzma.dll output\
xcopy /E build\Release\resources output\resources\
```

Copies all required runtime files to the output directory.

## Result

After running the script, your output directory contains:

```
output/
├── MyApp_Setup.exe     (Installer with embedded data)
├── DuiLib.dll          (GUI framework)
├── liblzma.dll         (Compression library)
└── resources/          (UI resources)
    ├── skins/
    │   ├── main.xml
    │   ├── welcome_page.xml
    │   ├── progress_page.xml
    │   ├── completion_page.xml
    │   └── license.xml
    ├── images/
    └── license.txt
```

## Testing

```cmd
cd output
MyApp_Setup.exe
```

**Expected:**
- ✅ GUI window appears
- ✅ No "加载资源文件失败" error
- ✅ Installation proceeds normally

## Distribution

### Option A: ZIP Archive (Recommended)

```cmd
REM Create a ZIP file with all files
powershell Compress-Archive -Path output\* -DestinationPath MyApp_Setup_v1.0.zip
```

Distribute `MyApp_Setup_v1.0.zip` to users.

**User instructions:**
1. Extract ZIP file
2. Run MyApp_Setup.exe

### Option B: Installer for the Installer

Use NSIS or Inno Setup to create a wrapper installer that:
1. Extracts all files to temp directory
2. Runs MyApp_Setup.exe
3. Cleans up after installation

### Option C: Self-Extracting Archive

Use 7-Zip or WinRAR to create a self-extracting archive:

```cmd
REM Using 7-Zip
7z a -sfx MyApp_Setup.exe output\*
```

## Troubleshooting

### "Packager not found" Error

**Cause:** Packager hasn't been built

**Fix:**
```cmd
cmake --build build --config Release --target packager
```

### "DuiLib.dll not found" Warning

**Cause:** Installer hasn't been built

**Fix:**
```cmd
cmake --build build --config Release --target installer
```

This builds the installer and copies DLLs to build\Release\.

### "resources directory not found" Warning

**Cause:** Resources weren't copied during installer build

**Fix:**
```cmd
REM Rebuild installer
cmake --build build --config Release --target installer

REM Verify resources exist
dir build\Release\resources\skins\main.xml
```

### Script Fails with Permission Error

**Cause:** Output directory is write-protected

**Fix:**
1. Run as administrator
2. Or choose a different output directory
3. Or check antivirus isn't blocking

## Integration with Build Process

### CMake Post-Build Command

Add to CMakeLists.txt:

```cmake
# Custom target to package with dependencies
add_custom_target(package_installer
    COMMAND ${CMAKE_COMMAND} -E echo "Packaging installer with dependencies..."
    COMMAND powershell -ExecutionPolicy Bypass -File 
            ${CMAKE_SOURCE_DIR}/scripts/package_with_dependencies.ps1
            -InputDir ${CMAKE_BINARY_DIR}/Release/input
            -OutputFile ${CMAKE_BINARY_DIR}/output/MyApp_Setup.exe
    DEPENDS packager installer
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Creating installer package with all dependencies"
)
```

Usage:
```cmd
cmake --build build --target package_installer
```

### CI/CD Integration

#### GitHub Actions

```yaml
- name: Package Installer
  run: |
    scripts/package_with_dependencies.bat build/Release/input output/MyApp_Setup.exe

- name: Upload Installer
  uses: actions/upload-artifact@v2
  with:
    name: installer-package
    path: output/
```

#### Jenkins

```groovy
stage('Package') {
    steps {
        bat 'scripts\\package_with_dependencies.bat build\\Release\\input output\\MyApp_Setup.exe'
        archiveArtifacts 'output/**'
    }
}
```

## Comparison with Direct Packager

### Direct Packager (❌ Incomplete)

```cmd
build\Release\packager.exe input output\MyApp_Setup.exe
```

**Result:**
- ✅ MyApp_Setup.exe created
- ❌ Missing DuiLib.dll
- ❌ Missing liblzma.dll
- ❌ Missing resources/
- ❌ Installer fails to run

### With Dependencies Script (✅ Complete)

```cmd
scripts\package_with_dependencies.bat input output\MyApp_Setup.exe
```

**Result:**
- ✅ MyApp_Setup.exe created
- ✅ DuiLib.dll copied
- ✅ liblzma.dll copied
- ✅ resources/ copied
- ✅ Installer runs successfully

## Best Practices

### For Development

1. **Use the script** for all packaging
2. **Test in clean directory** before distribution
3. **Version your output** (e.g., MyApp_v1.0_Setup.exe)

### For Release

1. **Clean build** before packaging:
   ```cmd
   cmake --build build --config Release --clean-first
   ```

2. **Run the packaging script**:
   ```cmd
   scripts\package_with_dependencies.bat input output\MyApp_v1.0_Setup.exe
   ```

3. **Test the package**:
   ```cmd
   mkdir test_install
   xcopy /E output test_install\
   cd test_install
   MyApp_v1.0_Setup.exe
   ```

4. **Create distribution archive**:
   ```cmd
   powershell Compress-Archive -Path output\* -DestinationPath MyApp_v1.0.zip
   ```

5. **Sign the installer** (optional but recommended):
   ```cmd
   signtool sign /f certificate.pfx /p password output\MyApp_v1.0_Setup.exe
   ```

## Summary

**Problem:** Packager creates incomplete installer package

**Solution:** Use `package_with_dependencies` script

**Command:**
```cmd
scripts\package_with_dependencies.bat input output\MyApp_Setup.exe
```

**Result:** Complete installer package ready for distribution

**Distribution:** ZIP the output directory or create self-extracting archive

**Status:** ✅ Production ready
