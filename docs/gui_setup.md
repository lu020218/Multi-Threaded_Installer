# GUI Setup Guide

## Overview

This document describes the GUI setup for the installer using DuiLib_Ultimate framework.

## Prerequisites

- Windows operating system
- CMake 3.16 or higher
- Visual Studio 2017 or higher (for building)
- DuiLib_Ultimate library (already included in `third_party/DuiLib_Ultimate`)

## Directory Structure

```
project/
├── third_party/
│   └── DuiLib_Ultimate/        # DuiLib framework
│       ├── DuiLib/             # Header files
│       ├── lib/                # Compiled libraries
│       └── bin/                # DLL files (if using dynamic linking)
├── resources/
│   ├── skins/                  # XML layout files
│   ├── images/                 # Image resources (logos, buttons, etc.)
│   └── license.txt             # License agreement text
├── src/
│   ├── gui/                    # GUI implementation files
│   └── installer/              # Existing installer code
├── include/
│   └── gui/                    # GUI header files
└── CMakeLists.txt
```

## Building with GUI Support

### Option 1: Enable GUI (Default)

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Option 2: Disable GUI

```bash
mkdir build
cd build
cmake -DBUILD_GUI=OFF ..
cmake --build . --config Release
```

## CMake Configuration

The GUI support is controlled by the `BUILD_GUI` option in CMakeLists.txt:

- **ON** (default): Builds installer with GUI support using DuiLib
- **OFF**: Builds console-only installer

When GUI is enabled:
- Adds `GUI_ENABLED` preprocessor definition
- Links DuiLib library and required Windows libraries (comctl32, GdiPlus, Imm32)
- Copies resources directory to output directory

## DuiLib Library

The project uses DuiLib_Ultimate, which is already included in the `third_party` directory.

### Library Files

- **Release**: `third_party/DuiLib_Ultimate/lib/DuiLib.lib`
- **Debug**: `third_party/DuiLib_Ultimate/lib/DuiLib_d.lib` (if available)

If the debug library is not found, the build system will fall back to the release library.

## Resources

### XML Layout Files

XML files defining the UI layout will be placed in `resources/skins/`:
- `main.xml` - Main window layout
- `welcome_page.xml` - Welcome page layout
- `progress_page.xml` - Progress page layout
- `completion_page.xml` - Completion page layout
- `license.xml` - License dialog layout

### Image Resources

Image files will be placed in `resources/images/`:
- `logo.png` - Application logo
- Button images (normal, hover, pushed states)
- Progress bar images
- Window control icons (minimize, close)

### License Text

The license agreement text is stored in `resources/license.txt` and will be displayed in the license dialog.

## Runtime Requirements

When running the installer with GUI:
- The `resources` directory must be present in the same directory as the executable
- All XML layout files must be present in `resources/skins/`
- All referenced images must be present in `resources/images/`

The build system automatically copies the resources directory to the output directory during build.

## Next Steps

1. Implement GUI components (Task 2-7 in tasks.md)
2. Create XML layout files (Task 2 in tasks.md)
3. Prepare image resources (Task 2.6 in tasks.md)
4. Integrate with existing installer code (Task 10 in tasks.md)

## Troubleshooting

### DuiLib library not found

If you see the warning "DuiLib library not found", ensure that:
1. The DuiLib_Ultimate directory exists in `third_party/`
2. The library file exists at `third_party/DuiLib_Ultimate/lib/DuiLib.lib`
3. If using a custom build, update the library path in CMakeLists.txt

### Resources not copied

If resources are not available at runtime:
1. Check that the build completed successfully
2. Verify the resources directory exists in the build output directory
3. Manually copy the resources directory if needed

### Linking errors

If you encounter linking errors related to DuiLib:
1. Ensure you're using the correct library for your build configuration
2. Check that all required Windows libraries are linked (comctl32, GdiPlus, Imm32)
3. Verify the C++ standard is set to C++17

## References

- DuiLib_Ultimate: https://github.com/qdtroy/DuiLib_Ultimate
- Design Document: `.kiro/specs/installer-gui-interface/design.md`
- Requirements Document: `.kiro/specs/installer-gui-interface/requirements.md`
- Tasks Document: `.kiro/specs/installer-gui-interface/tasks.md`
