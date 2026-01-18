# DuiLib Build Fixes Applied

## Summary

Fixed compilation errors related to Unicode/MBCS character set mismatch and DuiLib API compatibility issues.

## Issues Fixed

### 1. Character Set Mismatch

**Problem**: DuiLib was compiled with MBCS but project was using Unicode, causing type conversion errors.

**Fix Applied**:
- Updated `CMakeLists.txt` to configure installer for MBCS character set
- Added `_MBCS` definition
- Removed Unicode definitions

**Changes in CMakeLists.txt**:
```cmake
if(BUILD_GUI)
    target_compile_definitions(installer PRIVATE GUI_ENABLED)
    
    # Configure character set to match DuiLib (MBCS)
    if(MSVC)
        target_compile_definitions(installer PRIVATE _MBCS)
        target_compile_definitions(installer PRIVATE UNICODE=0 _UNICODE=0)
        message(STATUS "Configured installer for MBCS character set (required for DuiLib)")
    endif()
```

### 2. Override Specifier Errors

**Problem**: Base class methods in DuiLib don't use `override` keyword, causing C3668 errors.

**Fix Applied**:
- Removed `override` keyword from all virtual method declarations in `gui_manager.h`

**Changes in gui_manager.h**:
```cpp
// Before:
virtual CDuiString GetSkinFolder() override;
virtual CDuiString GetSkinFile() override;
virtual LPCTSTR GetWindowClassName() const override;
virtual void Notify(TNotifyUI& msg) override;
virtual void InitWindow() override;
virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

// After:
virtual CDuiString GetSkinFolder();
virtual CDuiString GetSkinFile();
virtual LPCTSTR GetWindowClassName() const;
virtual void Notify(TNotifyUI& msg);
virtual void InitWindow();
virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
```

### 3. Incorrect Member Variable Name

**Problem**: Code referenced `m_PaintManager` but DuiLib's WindowImplBase provides `m_pm`.

**Fix Applied**:
- Replaced all occurrences of `m_PaintManager` with `m_pm` in `gui_manager.cpp`

**Example**:
```cpp
// Before:
m_pTabPages = static_cast<CTabLayoutUI*>(m_PaintManager.FindControl(_T("pages")));

// After:
m_pTabPages = static_cast<CTabLayoutUI*>(m_pm.FindControl(_T("pages")));
```

### 4. String Conversion Issues

**Problem**: Cannot convert `std::wstring` (Unicode) to `LPCTSTR` (MBCS `const char*`).

**Fix Applied**:
- Added string conversion helper function in `gui_manager.cpp`
- Created `WSTR_TO_TSTR` macro for easy conversion
- Updated all `SetText()` calls to use the conversion macro

**Changes in gui_manager.cpp**:
```cpp
// Added at top of file:
static std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
    return result;
}

#define WSTR_TO_TSTR(wstr) WStringToString(wstr).c_str()

// Usage:
// Before:
pAppName->SetText(m_config.applicationName.c_str());

// After:
pAppName->SetText(WSTR_TO_TSTR(m_config.applicationName));
```

### 5. Window Creation String

**Problem**: `Create()` method received Unicode string literal in MBCS mode.

**Fix Applied**:
- Changed `L"安装向导"` to `_T("安装向导")` in `main.cpp`
- `_T()` macro automatically selects correct string type based on character set

**Changes in main.cpp**:
```cpp
// Before:
pFrame->Create(NULL, L"安装向导", UI_WNDSTYLE_FRAME, 0L, 0, 0, 800, 600);

// After:
pFrame->Create(NULL, _T("安装向导"), UI_WNDSTYLE_FRAME, 0L, 0, 0, 800, 600);
```

## Files Modified

1. **CMakeLists.txt**
   - Added MBCS configuration for installer target

2. **include/gui/gui_manager.h**
   - Removed `override` keywords from virtual methods

3. **src/gui/gui_manager.cpp**
   - Added string conversion helper function
   - Replaced `m_PaintManager` with `m_pm` (all occurrences)
   - Updated all `SetText()` calls to use `WSTR_TO_TSTR` macro

4. **src/installer/main.cpp**
   - Changed window title to use `_T()` macro

## Testing

After applying these fixes, the project should compile without errors. To verify:

```cmd
cd build
cmake --build . --config Release --target installer
```

Expected result: Clean build with no errors.

## Additional Notes

### Why MBCS Instead of Unicode?

The DuiLib library in `third_party/DuiLib_Ultimate` was compiled with MBCS character set. We have two options:

1. **Configure project for MBCS** (chosen solution)
   - Pros: No need to rebuild DuiLib, quick fix
   - Cons: Project uses MBCS instead of Unicode

2. **Rebuild DuiLib with Unicode**
   - Pros: Project can use Unicode throughout
   - Cons: Requires rebuilding third-party library

We chose option 1 for simplicity and to avoid modifying third-party code.

### String Conversion Performance

The `WStringToString` conversion function is called frequently. For better performance in production:

1. Cache converted strings where possible
2. Consider using `std::string` internally if MBCS is permanent
3. Or rebuild DuiLib with Unicode support

### Future Improvements

If Unicode support is required:

1. Rebuild DuiLib with Unicode character set:
   - Open DuiLib solution in Visual Studio
   - Project Properties → General → Character Set → "Use Unicode Character Set"
   - Rebuild all configurations
   - Replace library files

2. Update CMakeLists.txt to remove MBCS configuration

3. Remove string conversion helpers from gui_manager.cpp

4. Change `_T()` macros back to `L""` literals

## Troubleshooting

If build still fails:

1. **Clean and rebuild**:
   ```cmd
   cd build
   cmake --build . --config Release --target clean
   cmake --build . --config Release --target installer
   ```

2. **Check DuiLib library**:
   - Verify `third_party/DuiLib_Ultimate/lib/DuiLib.lib` exists
   - Check if it's MBCS or Unicode build

3. **Verify character set**:
   - Check project properties in Visual Studio
   - Should show "Multi-Byte Character Set"

4. **Check for remaining Unicode strings**:
   ```cmd
   grep -r "L\"" src/gui/
   ```
   Replace with `_T("")` or convert using helper function

## References

- DuiLib Documentation: Character set configuration
- MSDN: [Character Sets](https://docs.microsoft.com/windows/win32/intl/character-sets)
- MSDN: [Generic-Text Mappings](https://docs.microsoft.com/cpp/c-runtime-library/generic-text-mappings)
- CMake: [target_compile_definitions](https://cmake.org/cmake/help/latest/command/target_compile_definitions.html)

## Support

For additional help:
1. See `docs/BUILD_TROUBLESHOOTING_DUILIB.md` for detailed troubleshooting
2. Check DuiLib community forums
3. Review build logs for specific errors
4. Contact development team with build output
