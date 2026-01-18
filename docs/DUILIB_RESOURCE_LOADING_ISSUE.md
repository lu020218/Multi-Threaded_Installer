# DuiLib Resource Loading Issue - Investigation Summary

## Problem Description

After implementing DuiLib static linking, the installer executable builds successfully but the GUI window fails to appear. The process hangs during the `WindowImplBase::Create()` call.

## Current Status

### What Works
- ✅ DuiLib static library integration (no DuiLib.dll dependency)
- ✅ Installer builds successfully (1.38MB with static DuiLib)
- ✅ Resource path detection (resources directory found correctly)
- ✅ `SetResourceType(UILIB_FILE)` called before window creation
- ✅ Resource path set with trailing backslash
- ✅ XML files exist and are accessible
- ✅ GUIManager object created successfully
- ✅ InstallConfig set successfully

### What Fails
- ❌ `WindowImplBase::Create()` hangs indefinitely
- ❌ No window appears
- ❌ No error messages or exceptions thrown
- ❌ Process exits with code 1 after timeout

## Investigation Steps Taken

### 1. Resource Path Configuration
- Added `CPaintManagerUI::SetResourceType(UILIB_FILE)` to tell DuiLib to load from file system
- Ensured resource path ends with backslash: `resources\`
- Verified `GetSkinFolder()` returns `"skins\\"`
- Verified `GetSkinFile()` returns `"main.xml"`

### 2. XML File Modifications
- Removed all image file references (button_normal.png, logo.png, etc.)
- Replaced with solid colors using `bkcolor` attribute
- Changed progress bar from images to `forecolor`/`bkcolor`
- Changed fonts from "微软雅黑" to "Arial" to avoid MBCS encoding issues

### 3. Simplified Test
- Created minimal `test_simple.xml` without Include tags
- Still hangs at Create() call

### 4. Debug Logging
Added extensive logging:
```
Instance path: E:\Work\GitHub\Multi-Threaded_Installer\build\Release\output\
Resource path: E:\Work\GitHub\Multi-Threaded_Installer\build\Release\output\resources\
Path exists: YES
Set resource path to: E:\Work\GitHub\Multi-Threaded_Installer\build\Release\output\resources\
Set resource type to UILIB_FILE
Created GUIManager successfully
Set install config
About to call Create()...
[HANGS HERE]
```

## Possible Root Causes

### 1. Static Linking Issue
The DuiLib static library might have been compiled with different settings than our project:
- Runtime library mismatch (we use `/MT`, DuiLib static lib should also use `/MT`)
- Character set mismatch (we use MBCS, DuiLib should also use MBCS)
- Missing initialization for static library mode

### 2. XML Parser Issue
DuiLib's XML parser might be:
- Failing silently on XML parsing errors
- Waiting for a resource that doesn't exist
- Stuck in an infinite loop during Include processing

### 3. Window Creation Issue
The Win32 window creation might be failing due to:
- Window class registration failure
- Invalid window style or parameters
- Missing message pump initialization

### 4. COM/OLE Initialization
Although COM is initialized with `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`, there might be:
- Additional OLE initialization required for DuiLib
- Thread affinity issues

## Debug Output

```
Extracting embedded resources to: C:\Users\LUHAIS~1\AppData\Local\Temp\MTInstaller_34776_189429640
No embedded resources found, will use external resources
Instance path: E:\Work\GitHub\Multi-Threaded_Installer\build\Release\output\
Resource path: E:\Work\GitHub\Multi-Threaded_Installer\build\Release\output\resources\
Path exists: YES
Set resource path to: E:\Work\GitHub\Multi-Threaded_Installer\build\Release\output\resources\
Set resource type to UILIB_FILE
Created GUIManager successfully
Set install config
About to call Create()...
[Process hangs and exits with code 1]
```

## Files Modified

### XML Resources (removed image references)
- `resources/skins/main.xml` - Changed to use solid colors instead of images
- `resources/skins/welcome_page.xml` - Removed logo.png reference
- `resources/skins/progress_page.xml` - Changed progress bar to use colors
- `resources/skins/completion_page.xml` - Removed logo.png reference
- `resources/skins/test_simple.xml` - Created minimal test XML

### Source Code
- `src/installer/main.cpp` - Added extensive debug logging, SetResourceType call, resource path trailing backslash
- `src/gui/gui_manager.cpp` - Temporarily changed to use test_simple.xml

### Build Configuration
- `CMakeLists.txt` - Already configured for static linking with `/MT` runtime

## Next Steps to Try

### 1. Check DuiLib Static Library Compilation Settings
- Verify the static library was compiled with `/MT` (not `/MD`)
- Verify it was compiled with MBCS (not Unicode)
- Check if there are any preprocessor definitions required for static linking

### 2. Try Dynamic Linking Temporarily
- Revert to using DuiLib.dll to see if the issue is specific to static linking
- If dynamic linking works, the problem is in the static library configuration

### 3. Add More Detailed Error Handling
- Wrap Create() in try-catch to catch any exceptions
- Check GetLastError() after Create() fails
- Add logging inside GUIManager::InitWindow() to see if it's called

### 4. Test with DuiLib Demo
- Try running one of the DuiLib demo applications to verify the library works
- Compare our initialization code with the demo code

### 5. Check for Missing Dependencies
- Verify all required Windows libraries are linked (comctl32.lib, GdiPlus.lib, Imm32.lib)
- Check if any additional initialization is required for static DuiLib

### 6. Simplify Further
- Try creating a standalone minimal DuiLib application outside of the installer
- Test if the issue is specific to our installer or a general DuiLib problem

## Workaround Options

If the static linking issue cannot be resolved quickly:

### Option 1: Use Dynamic Linking
- Keep DuiLib.dll as an external dependency
- Package it with the installer
- This defeats the single-file installer goal but gets GUI working

### Option 2: Embed DuiLib.dll
- Embed DuiLib.dll as a resource in the installer
- Extract it to temp directory at runtime
- Load it dynamically
- Still achieves single-file distribution

### Option 3: Use Different GUI Framework
- Consider using Win32 API directly (more work)
- Consider using a different GUI library that's easier to statically link
- Consider using Qt or wxWidgets

## References

- DuiLib_Ultimate documentation: `third_party/DuiLib_Ultimate/Help/`
- DuiLib demos: `third_party/DuiLib_Ultimate/Demos/`
- Static library location: `third_party/DuiLib_Ultimate/lib_static/DuiLib.lib`

## Contact Points

If you encounter this issue:
1. Check if DuiLib demos work on your system
2. Verify the static library was built correctly
3. Try the dynamic linking workaround first
4. Consider reaching out to DuiLib_Ultimate community for static linking guidance

