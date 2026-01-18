# Manual Dependency Copy Guide

## Overview

If the packager's automatic dependency copying doesn't work, you can manually copy the required runtime files to your installer output directory.

## Required Files

The installer needs these files in the same directory:

1. **Installer executable** - Your packaged installer.exe
2. **DuiLib.dll** - GUI framework (1.6 MB)
3. **liblzma.dll** - LZMA compression (185 KB)
4. **resources/** - Directory with XML layouts and images

## Method 1: Using the Batch Script

A batch script is provided for easy copying:

```cmd
copy_installer_dependencies.bat output_directory
```

### Example

```cmd
REM Copy dependencies to the same directory as your installer
copy_installer_dependencies.bat C:\MyInstaller
```

This will copy:
- `build\Release\DuiLib.dll` → `C:\MyInstaller\DuiLib.dll`
- `build\Release\liblzma.dll` → `C:\MyInstaller\liblzma.dll`
- `build\Release\resources\` → `C:\MyInstaller\resources\`

## Method 2: Manual Copy

### Step 1: Copy DLL Files

```cmd
copy build\Release\DuiLib.dll output_directory\
copy build\Release\liblzma.dll output_directory\
```

### Step 2: Copy Resources Directory

```cmd
xcopy /E /I build\Release\resources output_directory\resources
```

### Step 3: Verify

```cmd
dir output_directory
```

You should see:
```
MyInstaller.exe
DuiLib.dll
liblzma.dll
resources\
```

## Method 3: PowerShell Script

```powershell
# Set paths
$sourceDir = "build\Release"
$targetDir = "output_directory"

# Create target directory
New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

# Copy DLLs
Copy-Item "$sourceDir\DuiLib.dll" -Destination $targetDir -Force
Copy-Item "$sourceDir\liblzma.dll" -Destination $targetDir -Force

# Copy resources
Copy-Item "$sourceDir\resources" -Destination $targetDir -Recurse -Force

Write-Host "Dependencies copied successfully!"
```

## Verification Checklist

After copying, verify all files are present:

- [ ] Installer executable exists
- [ ] DuiLib.dll exists (should be ~1.6 MB)
- [ ] liblzma.dll exists (should be ~185 KB)
- [ ] resources\skins\main.xml exists
- [ ] resources\skins\welcome_page.xml exists
- [ ] resources\skins\progress_page.xml exists
- [ ] resources\skins\completion_page.xml exists
- [ ] resources\skins\license.xml exists
- [ ] resources\license.txt exists

## Testing the Installer

After copying dependencies:

```cmd
cd output_directory
MyInstaller.exe
```

Expected result:
- ✅ GUI window appears
- ✅ No DLL errors
- ✅ No resource loading errors
- ✅ All pages display correctly

## Troubleshooting

### DuiLib.dll not found

**Symptom:** Error message "由于找不到DuiLib.dll"

**Solution:**
1. Verify DuiLib.dll is in the same directory as the installer
2. Check file size is approximately 1.6 MB
3. Ensure it's the correct architecture (x64)

### Resource loading failed

**Symptom:** Error message "加载资源文件失败：main.xml"

**Solution:**
1. Verify resources directory exists
2. Check resources\skins\main.xml exists
3. Ensure directory structure is correct:
   ```
   resources\
   ├── skins\
   │   ├── main.xml
   │   ├── welcome_page.xml
   │   ├── progress_page.xml
   │   ├── completion_page.xml
   │   └── license.xml
   ├── images\
   └── license.txt
   ```

### liblzma.dll not found

**Symptom:** Installer fails to decompress data

**Solution:**
1. Verify liblzma.dll is in the same directory
2. Check file size is approximately 185 KB

## Packaging for Distribution

When creating a distribution package:

### Option 1: ZIP Archive

```cmd
REM Create a ZIP file with all dependencies
powershell Compress-Archive -Path output_directory\* -DestinationPath MyInstaller_v1.0.zip
```

### Option 2: Self-Extracting Archive

Use a tool like 7-Zip or WinRAR to create a self-extracting archive that includes all files.

### Option 3: Installer for the Installer

Create a simple NSIS or Inno Setup script that extracts all files to a temporary directory and runs the installer.

## Why Manual Copy May Be Needed

The packager's automatic copying may fail if:

1. **Template directory not found**
   - Packager can't locate build/Release or build/Debug
   - Solution: Run packager from project root

2. **Files don't exist**
   - Installer hasn't been built yet
   - Solution: Build installer first: `cmake --build build --config Release --target installer`

3. **Permission issues**
   - Can't write to output directory
   - Solution: Run with appropriate permissions or choose different output directory

4. **Path issues**
   - Relative paths don't resolve correctly
   - Solution: Use absolute paths or run from project root

## Automation

To automate dependency copying in your build process:

### CMake Post-Build Command

Add to CMakeLists.txt:

```cmake
add_custom_command(TARGET packager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/Release/DuiLib.dll
        ${CMAKE_BINARY_DIR}/output/DuiLib.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/Release/liblzma.dll
        ${CMAKE_BINARY_DIR}/output/liblzma.dll
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/Release/resources
        ${CMAKE_BINARY_DIR}/output/resources
    COMMENT "Copying runtime dependencies to output directory"
)
```

### Build Script

Create a build_and_package.bat script:

```batch
@echo off
echo Building installer...
cmake --build build --config Release --target installer

echo Building packager...
cmake --build build --config Release --target packager

echo Running packager...
build\Release\packager.exe input output\MyInstaller.exe

echo Copying dependencies...
call copy_installer_dependencies.bat output

echo Done!
```

## Summary

While the packager should automatically copy runtime dependencies, manual copying provides a reliable fallback. Use the provided batch script or follow the manual steps to ensure all required files are present in your installer distribution.

**Key Point:** All runtime files (DLLs and resources) must be in the same directory as the installer executable for it to work correctly.
