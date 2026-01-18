# DuiLib DLL Runtime Error Fix

## Problem

When running the installer, the following error occurred:
```
由于找不到DuiLib.dll，无法继续执行代码。重新安装程序可能会解决此问题。
```

Translation: "Unable to continue executing code because DuiLib.dll was not found. Reinstalling the program may resolve this issue."

## Root Cause

The CMakeLists.txt configuration was missing the step to copy DuiLib.dll to the output directory. While the installer executable was linked against DuiLib.lib at compile time, the DuiLib.dll was not available at runtime.

## Solution

Modified the `copy_dlls_to_target()` function in CMakeLists.txt to include DuiLib.dll copying:

```cmake
# Function to copy DLLs to target directory
function(copy_dlls_to_target target_name)
    if(LIBLZMA_DLL_PATH)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBLZMA_DLL_PATH}"
            $<TARGET_FILE_DIR:${target_name}>
            COMMENT "Copying LZMA DLL to output directory"
        )
    endif()
    
    # Copy DuiLib DLL if GUI is enabled
    if(BUILD_GUI)
        set(DUILIB_DLL_PATH "${THIRD_PARTY_DIR}/DuiLib_Ultimate/bin/DuiLib.dll")
        if(EXISTS ${DUILIB_DLL_PATH})
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DUILIB_DLL_PATH}"
                $<TARGET_FILE_DIR:${target_name}>
                COMMENT "Copying DuiLib DLL to output directory"
            )
        else()
            message(WARNING "DuiLib.dll not found at ${DUILIB_DLL_PATH}")
        endif()
    endif()
endfunction()
```

## Files Modified

- `CMakeLists.txt` - Added DuiLib.dll copying to `copy_dlls_to_target()` function

## DLL Location

**Source**: `third_party/DuiLib_Ultimate/bin/DuiLib.dll`
**Destination**: `build/Release/DuiLib.dll` (same directory as installer.exe)

## Verification

After rebuilding, verify that DuiLib.dll is present in the output directory:

```cmd
dir build\Release\*.dll
```

Expected output:
```
DuiLib.dll      (1.6 MB)
liblzma.dll     (185 KB)
```

## Build Command

To rebuild with the fix:

```cmd
cmake --build build --config Release --target installer
```

## Build Output

Successful build output should show:
```
Copying LZMA DLL to output directory
Copying DuiLib DLL to output directory
Copying GUI resources (XML layouts and images) to output directory
```

## Testing

After rebuilding, test the installer:

```cmd
cd build\Release
installer.exe
```

The installer should now launch without the DLL error.

## For Release Packages

When preparing a release package, ensure the following DLLs are included:

1. **DuiLib.dll** - GUI framework (1.6 MB)
2. **liblzma.dll** - LZMA compression (185 KB)

These DLLs must be in the same directory as installer.exe.

## Deployment Checklist

When deploying the installer:

- ✅ installer.exe
- ✅ DuiLib.dll
- ✅ liblzma.dll
- ✅ resources/ folder (XML layouts and images)

## Related Documentation

- `docs/BUILD_AND_DEPLOYMENT.md` - Complete build and deployment guide
- `docs/RELEASE_CHECKLIST.md` - Release preparation checklist
- `docs/COMPILATION_SUCCESS_SUMMARY.md` - Compilation fixes summary

## Technical Notes

### Why DuiLib Uses DLL

DuiLib_Ultimate is distributed as a dynamic library (DLL) rather than a static library. This means:

**Advantages:**
- Smaller executable size
- Shared library can be updated independently
- Faster linking during development

**Disadvantages:**
- DLL must be distributed with the executable
- DLL version compatibility must be maintained
- Slightly slower startup time

### Alternative: Static Linking

If you prefer to avoid DLL dependencies, you can rebuild DuiLib as a static library:

1. Open `third_party/DuiLib_Ultimate/DuiLib.sln` in Visual Studio
2. Change project configuration to static library (.lib)
3. Rebuild DuiLib
4. Update CMakeLists.txt to link against the static library
5. Remove DLL copying code

**Note:** Static linking will increase the installer.exe size by approximately 1.6 MB.

## Troubleshooting

### DLL Still Not Found

If the error persists after rebuilding:

1. **Verify DLL exists in output directory:**
   ```cmd
   dir build\Release\DuiLib.dll
   ```

2. **Check DLL is in same directory as EXE:**
   ```cmd
   cd build\Release
   dir
   ```
   Both installer.exe and DuiLib.dll should be listed.

3. **Verify DLL architecture matches:**
   - Installer is built for x64
   - DuiLib.dll must also be x64
   - Check with: `dumpbin /headers DuiLib.dll | findstr machine`

4. **Check for missing dependencies:**
   Use Dependency Walker or similar tool to check if DuiLib.dll has missing dependencies.

### Wrong DLL Version

If you get version mismatch errors:

1. Ensure DuiLib.lib and DuiLib.dll are from the same build
2. Check timestamps match:
   ```cmd
   dir third_party\DuiLib_Ultimate\lib\DuiLib.lib
   dir third_party\DuiLib_Ultimate\bin\DuiLib.dll
   ```

3. If timestamps don't match, rebuild DuiLib from source

### DLL Conflicts

If you have multiple versions of DuiLib.dll on your system:

1. Check system PATH for other DuiLib.dll locations
2. Ensure the correct DLL is being loaded
3. Use Process Monitor to see which DLL is being loaded

## Summary

The runtime error was caused by missing DuiLib.dll in the output directory. The fix adds automatic DLL copying during the build process, ensuring all required runtime dependencies are available.

**Status**: ✅ Fixed

**Date**: 2026-01-18

**Impact**: Installer now runs successfully without DLL errors
