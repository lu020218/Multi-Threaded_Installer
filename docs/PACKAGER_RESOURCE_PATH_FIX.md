# Packager Resource Path Fix

## Issue Summary

Packager-generated installers were failing with "加载资源文件失败：main.xml" error, even though the packager was correctly copying all runtime dependencies (DuiLib.dll, liblzma.dll, and resources/) to the output directory.

## Root Cause

The issue was in the `EmbeddedResourceManager::extractResources()` function in `src/installer/embedded_resources.cpp`:

1. The function would create a temporary directory for extracted resources
2. It would attempt to extract embedded resources (which returned empty because `readEmbeddedResourceFromFile()` is not yet implemented)
3. **Critical Bug**: Even when NO resources were extracted, the function would still return the temp directory path
4. The installer would then try to use this empty temp directory instead of falling back to external resources
5. This caused the "resource file not found" error

## Solution

Modified `EmbeddedResourceManager::extractResources()` to:

1. Track whether ANY resources were actually extracted using an `anyExtracted` boolean flag
2. If no resources were extracted, cleanup the temp directory and return an empty string
3. This allows the installer to properly fall back to external resources using `CPaintManagerUI::GetInstancePath() + "resources"`

### Code Changes

**File**: `src/installer/embedded_resources.cpp`

```cpp
std::string EmbeddedResourceManager::extractResources() {
    // ... create temp directory ...
    
    bool anyExtracted = false;  // NEW: Track extraction success
    
    // Try to extract each resource
    auto duilib = getEmbeddedResource("DUILIB_DLL");
    if (!duilib.empty()) {
        if (extractFile("DuiLib.dll", duilib)) {
            anyExtracted = true;  // Mark as extracted
        }
    }
    
    // ... extract other resources ...
    
    // NEW: Check if any resources were extracted
    if (!anyExtracted) {
        std::cout << "No embedded resources found, will use external resources" << std::endl;
        cleanup();  // Remove empty temp directory
        return "";  // Return empty to trigger fallback
    }
    
    m_extracted = true;
    return m_resourcePath;
}
```

## Additional Improvements

### Debug Logging in main.cpp

Added detailed debug logging to help diagnose resource path issues:

```cpp
if (resourcePath.IsEmpty()) {
    CDuiString instancePath = CPaintManagerUI::GetInstancePath();
    resourcePath = instancePath + _T("resources");
    
    // Debug output
    std::wcout << L"Instance path: " << instancePath.GetData() << std::endl;
    std::wcout << L"Resource path: " << resourcePath.GetData() << std::endl;
    std::wcout << L"Path exists: " << (PathFileExists(resourcePath) ? L"YES" : L"NO") << std::endl;
    
    if (!PathFileExists(resourcePath)) {
        // Show detailed error message with paths
        // ...
    }
}
```

### Packager Error Reporting

Modified `src/packager/main.cpp` to show detailed configuration errors:

```cpp
if (!configManager.initialize(inputPath)) {
    console.showError("Failed to initialize configuration");
    std::string error = configManager.getLastError();
    if (!error.empty()) {
        console.showError("Configuration error: " + error);
    }
    return 1;
}
```

## Verification

### Test Results

1. **Packager Output**: Successfully copies all dependencies
   ```
   Copying runtime dependencies from: "build/Release"
     Copied: DuiLib.dll
     Copied: liblzma.dll
     Copied: resources/ directory
   ```

2. **Installer Debug Output**: Shows correct fallback behavior
   ```
   Extracting embedded resources to: C:\Users\...\Temp\MTInstaller_...
   No embedded resources found, will use external resources
   Instance path: E:\...\build\Release\output\
   Resource path: E:\...\build\Release\output\resources
   Path exists: YES
   ```

3. **Result**: Packager-generated installer now launches successfully with GUI

## Files Modified

- `src/installer/embedded_resources.cpp` - Fixed resource extraction logic
- `src/installer/main.cpp` - Added debug logging for resource paths
- `src/packager/main.cpp` - Improved error reporting
- `test_input/packager.json` - Fixed configuration format

## Configuration Format Note

The packager expects configuration files with these field names:
- `Version` (not `configVersion`)
- `AppName` (not `applicationName`)  
- `InstallDir` (not `defaultInstallDir`)

Example:
```json
{
  "Version": "1.0.0",
  "AppName": "TestApp",
  "InstallDir": "%ProgramFiles%\\TestApp",
  "Folder": {
    "InstallDir": "testfolder"
  }
}
```

## Next Steps

The packager-generated installers now work correctly with external resources. For a true single-file installer solution, we still need to:

1. Complete the `readEmbeddedResourceFromFile()` implementation in `src/installer/embedded_resources.cpp`
2. Test the `scripts/embed_resources.ps1` script to embed resources into the executable
3. Consider static linking of DuiLib to eliminate the DLL dependency

## Status

✅ **FIXED**: Packager-generated installers now work correctly with external resources
⏳ **PENDING**: Single-file embedded resource implementation (Phase 2)
