# Single-File Installer Status and Roadmap

## Current Status

### What Works Now

The installer currently requires these files to be distributed together:

```
MyInstaller.exe      (Main executable with embedded data)
DuiLib.dll          (GUI framework - 1.6 MB)
liblzma.dll         (LZMA compression - 185 KB)
resources/          (XML layouts and images)
├── skins/
│   ├── main.xml
│   ├── welcome_page.xml
│   ├── progress_page.xml
│   ├── completion_page.xml
│   └── license.xml
├── images/
└── license.txt
```

### Graceful Degradation

The installer now includes fallback logic:

1. **Check for resources** on startup
2. **If resources missing**: Show friendly error message
3. **Fallback to console mode**: Run installation without GUI

This ensures the installer always works, even if GUI resources are missing.

## Why Not Single-File Yet?

### Challenge 1: DuiLib DLL Dependency

**Problem:** DuiLib_Ultimate is distributed as a DLL, not a static library.

**Current State:**
- DuiLib.lib is an import library (for linking to DLL)
- DuiLib.dll must be present at runtime
- Size: ~1.6 MB

**Solutions:**

#### Option A: Static Linking (Recommended)
Rebuild DuiLib as a static library:

**Pros:**
- ✅ True single-file installer
- ✅ No DLL dependencies
- ✅ Simpler deployment

**Cons:**
- ❌ Requires rebuilding DuiLib from source
- ❌ Increases .exe size by ~1.6 MB
- ❌ Longer compile times

**Steps:**
1. Open `third_party/DuiLib_Ultimate/DuiLib.sln`
2. Change Configuration Type to "Static Library (.lib)"
3. Rebuild DuiLib
4. Update CMakeLists.txt to link static library
5. Remove DLL copying code

#### Option B: Embed and Extract DLL
Embed DuiLib.dll in installer, extract at runtime:

**Pros:**
- ✅ Single .exe file
- ✅ No source code changes to DuiLib

**Cons:**
- ❌ Must extract DLL to temp directory
- ❌ Antivirus may flag as suspicious
- ❌ Cleanup complexity

**Implementation:**
1. Embed DuiLib.dll as binary resource
2. Extract to %TEMP% on startup
3. Load DLL from temp location
4. Clean up on exit

#### Option C: Use Different GUI Framework
Switch to a framework with better static linking support:

**Options:**
- Qt (can be statically linked)
- wxWidgets (can be statically linked)
- Win32 API directly (no dependencies)

**Pros:**
- ✅ Full control over dependencies
- ✅ True single-file possible

**Cons:**
- ❌ Major rewrite required
- ❌ Learning curve
- ❌ Time investment

### Challenge 2: Resource Files (XML, Images)

**Problem:** DuiLib loads XML layouts and images from disk.

**Current State:**
- XML files define UI layout
- Images for buttons, logos, etc.
- Must be in resources/ directory

**Solutions:**

#### Option A: Windows Resource Files (Recommended)
Embed resources in .exe using .rc files:

**Pros:**
- ✅ Standard Windows approach
- ✅ No extraction needed
- ✅ Resources in memory

**Cons:**
- ❌ Requires code changes to load from resources
- ❌ DuiLib may not support this directly

**Implementation:**
1. Create installer.rc resource file
2. Add XML and images as RCDATA resources
3. Modify DuiLib loading code to read from resources
4. May require DuiLib source modifications

#### Option B: Extract to Temp Directory
Extract resources on startup:

**Pros:**
- ✅ Works with current DuiLib code
- ✅ No DuiLib modifications needed

**Cons:**
- ❌ Disk I/O on every run
- ❌ Temp directory cleanup
- ❌ Potential permission issues

**Implementation:**
1. Embed resources as binary data
2. Extract to %TEMP%\\InstallerResources_{PID}
3. Point DuiLib to temp directory
4. Clean up on exit

#### Option C: Minimal Embedded Resources
Use minimal hardcoded UI:

**Pros:**
- ✅ No external files needed
- ✅ Smaller size

**Cons:**
- ❌ Less flexible UI
- ❌ Harder to customize

## Recommended Approach

### Phase 1: Static Link DuiLib (High Priority)

This eliminates the DuiLib.dll dependency:

1. **Rebuild DuiLib as static library**
   ```
   - Open third_party/DuiLib_Ultimate/DuiLib.sln
   - Project Properties → Configuration Type → Static Library (.lib)
   - Rebuild for Release x64
   ```

2. **Update CMakeLists.txt**
   ```cmake
   # Change from:
   set(DUILIB_LIB ${THIRD_PARTY_DIR}/DuiLib_Ultimate/lib/DuiLib.lib)
   
   # To:
   set(DUILIB_STATIC_LIB ${THIRD_PARTY_DIR}/DuiLib_Ultimate/lib/DuiLib_static.lib)
   target_link_libraries(installer ${DUILIB_STATIC_LIB} ...)
   
   # Remove DLL copying code
   ```

3. **Test**
   - Build installer
   - Verify no DuiLib.dll needed
   - Test all GUI functionality

**Result:** Installer.exe + liblzma.dll + resources/

### Phase 2: Embed Resources (Medium Priority)

Embed XML and image resources:

1. **Create resource file**
   ```rc
   // installer.rc
   IDR_MAIN_XML        RCDATA  "resources/skins/main.xml"
   IDR_WELCOME_XML     RCDATA  "resources/skins/welcome_page.xml"
   // ... more resources
   ```

2. **Modify resource loading**
   ```cpp
   // Load XML from embedded resources instead of disk
   HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(IDR_MAIN_XML), RT_RCDATA);
   HGLOBAL hData = LoadResource(NULL, hRes);
   void* pData = LockResource(hData);
   ```

3. **Update DuiLib integration**
   - May require custom resource loader
   - Or extract to memory stream

**Result:** Installer.exe + liblzma.dll

### Phase 3: Handle liblzma.dll (Low Priority)

Options for liblzma:

1. **Static link** - Rebuild liblzma as static library
2. **Embed and extract** - Extract DLL to temp on startup
3. **Alternative library** - Use zlib or other compression

**Result:** Single installer.exe file

## Implementation Priority

### Immediate (This Release)
- ✅ Graceful fallback to console mode
- ✅ Clear error messages about missing files
- ✅ Documentation on required files

### Short Term (Next Release)
- [ ] Static link DuiLib
- [ ] Embed XML resources
- [ ] Test single-file (except liblzma.dll)

### Long Term (Future)
- [ ] Embed liblzma or use alternative
- [ ] True single-file installer
- [ ] Automated build process

## Current Workaround

Until single-file is implemented, use the packager's automatic dependency copying:

```cmd
# Build everything
cmake --build build --config Release

# Package with dependencies
build\Release\packager.exe input output\MyInstaller.exe

# This creates:
output\
├── MyInstaller.exe
├── DuiLib.dll          (copied automatically)
├── liblzma.dll         (copied automatically)
└── resources\          (copied automatically)
```

Distribute the entire `output\` directory as a ZIP file.

## Testing Current Implementation

The installer now handles missing resources gracefully:

### Test 1: With Resources (GUI Mode)
```cmd
cd build\Release
installer.exe
```
**Expected:** GUI window appears

### Test 2: Without Resources (Console Mode)
```cmd
cd build\Release
ren resources resources_backup
installer.exe
```
**Expected:** 
1. Error dialog: "GUI资源文件未找到"
2. Falls back to console mode
3. Installation proceeds in console

### Test 3: Packaged Installer
```cmd
cd output
MyInstaller.exe
```
**Expected:** GUI works (all dependencies present)

## Conclusion

**Current State:** Multi-file installer with graceful degradation

**Goal:** True single-file installer

**Next Step:** Static link DuiLib (eliminates largest dependency)

**Timeline:** 
- Phase 1 (Static DuiLib): 1-2 days
- Phase 2 (Embed resources): 2-3 days  
- Phase 3 (Handle liblzma): 1 day

**Total Effort:** ~1 week for complete single-file solution

## References

- DuiLib Documentation: `third_party/DuiLib_Ultimate/Help/`
- Windows Resources: https://docs.microsoft.com/en-us/windows/win32/menurc/resources
- Static Linking Guide: `docs/BUILD_AND_DEPLOYMENT.md`
