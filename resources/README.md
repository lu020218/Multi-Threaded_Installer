# Resources Directory

This directory contains all GUI resources for the installer.

## Directory Structure

```
resources/
├── skins/          # XML layout files for DuiLib
├── images/         # Image resources (logos, buttons, icons)
└── license.txt     # License agreement text
```

## Skins Directory

XML files defining the UI layout using DuiLib syntax. These files will include:

- `main.xml` - Main window framework with title bar and TabLayout container
- `welcome_page.xml` - Welcome page with logo, path selection, and license agreement
- `progress_page.xml` - Progress page with progress bar and status information
- `completion_page.xml` - Completion page with result message and post-install options
- `license.xml` - License agreement dialog window
- `styles.xml` - Shared style definitions (optional)

## Images Directory

Image resources used by the UI. Required images include:

### Application Branding
- `logo.png` - Application logo (64x64 pixels recommended)

### Button States
- `button_normal.png` - Button normal state
- `button_hover.png` - Button hover state
- `button_pushed.png` - Button pressed state
- `button_primary_*.png` - Primary button variants
- `button_secondary_*.png` - Secondary button variants

### Progress Bar
- `progress_fore.png` - Progress bar foreground
- `progress_bk.png` - Progress bar background

### Window Controls
- `min.png` - Minimize button icon
- `close.png` - Close button icon

## License Text

The `license.txt` file contains the license agreement text that will be displayed in the license dialog. Replace the placeholder text with your actual license agreement before distribution.

## Usage

These resources are automatically copied to the build output directory by CMake when `BUILD_GUI=ON`. The installer will load these resources at runtime from the `resources/` directory relative to the executable.

## Notes

- All image files should be in PNG format for transparency support
- XML files must use UTF-8 encoding
- Image dimensions should be appropriate for the UI design (see design.md for specifications)
- For high DPI support, consider providing @2x versions of images
