# DuiLib Build Troubleshooting

## Issue: Unicode/MBCS Character Set Mismatch

### Problem

Compilation errors when building with DuiLib:
- `error C2664: cannot convert parameter from 'const wchar_t *' to 'LPCTSTR'`
- `error C3668: method with override specifier 'override' did not override any base class methods`
- `error C2065: 'm_PaintManager': undeclared identifier`

### Root Cause

DuiLib was compiled with **MBCS (Multi-Byte Character Set)** but the project is using **Unicode**. This causes type mismatches between `wchar_t*` (Unicode) and `char*` (MBCS).

### Solution

Configure the project to use MBCS to match DuiLib's configuration.

## Fix Steps

### Option 1: Configure Project for MBCS (Recommended)

Add to `CMakeLists.txt` before the installer target:

```cmake
# Configure for MBCS to match DuiLib
if(BUILD_GUI AND MSVC)
    # Remove Unicode definitions
    add_definitions(-UMBCS -U_UNICODE -UUNICODE)
    # Add MBCS definition
    add_definitions(-D_MBCS)
    message(STATUS "Configured for MBCS character set (required for DuiLib)")
endif()
```

### Option 2: Rebuild DuiLib with Unicode

If you prefer to keep Unicode:

1. Open DuiLib solution in Visual Studio
2. Project Properties → Configuration Properties → General
3. Change "Character Set" to "Use Unicode Character Set"
4. Rebuild DuiLib
5. Replace the library file in `third_party/DuiLib_Ultimate/lib/`

### Option 3: Use String Conversion Macros

Add conversion macros in code:

```cpp
// In gui_manager.cpp, add at top:
#ifdef UNICODE
#define DUI_STR(x) L##x
#define STD_TO_DUI(x) (x.c_str())
#else
#define DUI_STR(x) x
// Convert wstring to string for MBCS
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
    return result;
}
#define STD_TO_DUI(x) WStringToString(x).c_str()
#endif
```

## Additional Fixes Required

### Fix 1: WindowImplBase Method Signatures

The base class methods in DuiLib don't use `override` keyword. Remove `override` from declarations:

```cpp
// In gui_manager.h, change:
virtual CDuiString GetSkinFolder() override;  // WRONG
virtual CDuiString GetSkinFile() override;    // WRONG

// To:
virtual CDuiString GetSkinFolder();  // CORRECT
virtual CDuiString GetSkinFile();    // CORRECT
```

### Fix 2: Add m_PaintManager Member

WindowImplBase provides `m_pm` not `m_PaintManager`. Use the correct member:

```cpp
// In gui_manager.cpp, change:
m_PaintManager.FindControl(...)  // WRONG

// To:
m_pm.FindControl(...)  // CORRECT
```

Or add accessor:
```cpp
CPaintManagerUI* GetPaintManager() { return &m_pm; }
```

### Fix 3: Create Method Signature

The `Create` method signature in DuiLib is:

```cpp
HWND Create(HWND hwndParent, LPCTSTR pstrName, DWORD dwStyle, DWORD dwExStyle, 
            int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, 
            int cx = CW_USEDEFAULT, int cy = CW_USEDEFAULT, 
            HMENU hMenu = NULL);
```

In MBCS mode, `LPCTSTR` is `const char*`, so convert the string:

```cpp
// In main.cpp, change:
pFrame->Create(NULL, L"安装向导", ...);  // WRONG in MBCS

// To:
pFrame->Create(NULL, "安装向导", ...);  // CORRECT in MBCS
// Or use conversion:
pFrame->Create(NULL, _T("安装向导"), ...);  // Works in both
```

## Complete Fix Implementation

### Step 1: Update CMakeLists.txt

```cmake
# Add after: if(BUILD_GUI)

if(MSVC)
    # Configure character set to match DuiLib (MBCS)
    add_definitions(-D_MBCS)
    remove_definitions(-DUNICODE -D_UNICODE)
    
    # Set character set for installer target
    set_target_properties(installer PROPERTIES
        COMPILE_DEFINITIONS "_MBCS"
    )
    
    message(STATUS "Configured installer for MBCS character set")
endif()
```

### Step 2: Update gui_manager.h

```cpp
// Remove 'override' keyword from these methods:
virtual CDuiString GetSkinFolder();
virtual CDuiString GetSkinFile();
virtual LPCTSTR GetWindowClassName() const;
virtual void Notify(TNotifyUI& msg);
virtual void InitWindow();
virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
```

### Step 3: Update gui_manager.cpp

```cpp
// Change all m_PaintManager to m_pm:
// Find: m_PaintManager
// Replace: m_pm

// For string conversions, add helper at top of file:
#include <atlconv.h>  // For string conversion

// Or use manual conversion:
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
    return result;
}

// Then use:
pLabel->SetText(WStringToString(m_config.applicationName).c_str());
```

### Step 4: Update main.cpp

```cpp
// Change Unicode strings to MBCS:
pFrame->Create(NULL, "安装向导", UI_WNDSTYLE_FRAME, 0L, 0, 0, 800, 600);

// Or use TCHAR macro:
pFrame->Create(NULL, _T("安装向导"), UI_WNDSTYLE_FRAME, 0L, 0, 0, 800, 600);
```

## Testing the Fix

After applying fixes:

```cmd
cd build
cmake --build . --config Release --clean-first
```

Verify no compilation errors.

## Alternative: Use Unicode-Compatible DuiLib Fork

Consider using a Unicode-compatible fork of DuiLib:
- **Duilib_Faw**: Modern fork with Unicode support
- **DuiLib_Redrain**: Another Unicode-compatible version

## Prevention

For future projects:
1. Check library character set before integration
2. Configure project character set early
3. Use TCHAR macros for portability
4. Document character set requirements

## References

- DuiLib Documentation: Character set configuration
- MSDN: Unicode and Character Sets
- CMake: Character set configuration

## Support

If issues persist:
1. Verify DuiLib library character set
2. Check all string conversions
3. Ensure consistent character set across project
4. Contact DuiLib community for support
