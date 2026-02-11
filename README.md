# Rust Installer System

A modern, high-performance installer system written in Rust with Tauri-based GUI support. This project provides a complete solution for creating and deploying Windows application installers with support for compression, multi-language UI, and automated deployment.

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Prerequisites](#prerequisites)
- [Building](#building)
- [Usage](#usage)
  - [Creating a Package](#creating-a-package)
  - [Installing a Package](#installing-a-package)
  - [Silent Installation](#silent-installation)
  - [Uninstallation](#uninstallation)
- [Configuration](#configuration)
- [Flow DSL Reference](#flow-dsl-reference)
- [UI Customization](#ui-customization)
- [Multi-Language Support](#multi-language-support)
- [Package Format](#package-format)
- [API Documentation](#api-documentation)
- [Testing](#testing)
- [License](#license)

## Overview

This project provides a complete installer solution consisting of five main components:

| Component | Description |
|-----------|-------------|
| `installer_shared` | Shared types, error definitions, and configuration models |
| `installer_core` | Core library with packaging and installation logic |
| `packager_cli` | Command-line tool for creating installer packages |
| `installer_cli` | Command-line installer for silent/automated deployments |
| `installer_gui` | Tauri-based graphical installer with modern UI |

## Features

### Core Features
- **Modern Package Format**: Custom binary format with efficient data organization
- **High-Performance Compression**: Zstd (default) and LZMA compression algorithms
- **Parallel Processing**: Multi-threaded compression and decompression using rayon
- **Data Integrity**: CRC32 checksums for all data blocks
- **Automatic Rollback**: Clean recovery on installation failure

### Platform Support
- **Windows-First Design**: Full Windows 10/11 support
- **Registry Integration**: Automatic uninstaller registration
- **Desktop Shortcuts**: Optional shortcut creation
- **Auto-Startup**: Configure application to start with Windows
- **UAC Support**: Automatic privilege elevation when needed

### User Interface
- **Tauri-Based GUI**: Modern, responsive web-based interface
- **Multi-Language Support**: Built-in i18n with easy localization
- **Customizable UI**: HTML/CSS/JS templates for branding
- **Silent Mode**: Command-line installation for automation

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Workspace Root                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────┐      ┌──────────────────┐           │
│  │  packager_cli    │      │  installer_cli   │           │
│  │  (CLI Tool)      │      │  (CLI Tool)      │           │
│  └────────┬─────────┘      └────────┬─────────┘           │
│           │                         │                      │
│           │         ┌───────────────┴──────────┐          │
│           │         │   installer_gui          │          │
│           │         │   (Tauri GUI)            │          │
│           │         └───────────┬──────────────┘          │
│           │                     │                          │
│           └─────────┬───────────┘                          │
│                     │                                      │
│           ┌─────────▼──────────┐                          │
│           │  installer_core    │                          │
│           │  (Core Logic)      │                          │
│           └─────────┬──────────┘                          │
│                     │                                      │
│           ┌─────────▼──────────┐                          │
│           │  installer_shared  │                          │
│           │  (Shared Types)    │                          │
│           └────────────────────┘                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## Prerequisites

### Build Requirements
- **Rust**: 1.75 or later ([Install Rust](https://rustup.rs/))
- **Windows SDK**: Windows 10 SDK or later
- **Node.js**: 18+ (for GUI development only)

### Runtime Requirements
- **Windows**: Windows 10 version 1903 or later
- **WebView2**: Required for GUI mode ([Download](https://developer.microsoft.com/en-us/microsoft-edge/webview2/))

## Building

### Quick Start

```bash
# Clone the repository
git clone https://github.com/example/rust-installer.git
cd rust-installer

# Build all crates in release mode
cargo build --release

# The binaries will be in target/release/
```

### Build Options

```bash
# Build only CLI tools (no GUI dependencies)
cargo build --release -p packager_cli -p installer_cli

# Build with all features
cargo build --release --all-features

# Build GUI application
cargo build --release -p installer_gui

# Run all tests
cargo test --all

# Run property-based tests (may take longer)
cargo test --all -- --ignored proptest

# Generate documentation
cargo doc --no-deps --open
```

### Cross-Compilation

The project is designed for Windows but can be cross-compiled:

```bash
# Add Windows target (from Linux/macOS)
rustup target add x86_64-pc-windows-msvc

# Build for Windows
cargo build --release --target x86_64-pc-windows-msvc
```

## Usage

### Creating a Package

The `packager_cli` tool creates installer packages from an input directory. By default it
auto-discovers configuration at `INPUT/config/packager.yaml` (or `INPUT/packager.yaml`).

```bash
# Basic usage (auto-discovered config in INPUT/config/packager.yaml)
packager --input ./my-app --output ./my-app-installer.mti

# Specify configuration file explicitly (override auto-discovery)
packager --input ./my-app --output ./installer.mti --config ./config/packager.yaml

# Override compression settings
packager --input ./my-app --output ./installer.mti \
    --compression-algorithm zstd \
    --compression-level 9

# Override UI resources directory
packager --input ./my-app --output ./installer.mti \
    --ui-resources ./custom-ui

# Verbose output
packager --input ./my-app --output ./installer.mti --verbose
```

### Installing a Package

The `installer_cli` tool installs packages created by the packager.

```bash
# Interactive installation (prompts for options)
installer --package ./installer.mti

# Specify installation directory
installer --package ./installer.mti --install-dir "C:\Program Files\MyApp"

# Skip optional features
installer --package ./installer.mti --no-shortcuts --no-registry
```

### Silent Installation

For automated deployments, use silent mode:

```bash
# Silent installation with defaults
installer --package ./installer.mti --silent

# Silent installation with custom directory
installer --package ./installer.mti --silent \
    --install-dir "C:\Program Files\MyApp"

# Silent installation with all options
installer --package ./installer.mti --silent \
    --install-dir "D:\Apps\MyApp" \
    --no-shortcuts \
    --no-registry

# Check exit code for success/failure
installer --package ./installer.mti --silent && echo "Success" || echo "Failed"
```

Exit codes:
- `0`: Installation successful
- `1`: General error
- `2`: Insufficient disk space
- `3`: Permission denied
- `4`: Package validation failed

### Uninstallation

```bash
# Uninstall using the installer
installer --package ./installer.mti --uninstall

# Or use the generated uninstaller in the install directory
"C:\Program Files\MyApp\uninstall.exe"
```

## Configuration

Create a `packager.yaml` file under `INPUT/config/packager.yaml` (recommended) or specify it
explicitly with `--config`. When using the recommended layout, `packager` will treat
`INPUT/payload/` as the install payload root.

Recommended layout:

```
my-app/
├─ payload/            # payload root (installed content)
│  ├─ app/
│  ├─ plugin/
│  └─ resources/       # runtime assets (e.g., component_manifest.yaml)
└─ config/             # packager config + UI + scripts
   ├─ packager.yaml
   ├─ flow.yaml
   ├─ scripts/
   └─ ui/
```

### Basic Configuration

```yaml
application_name: "MyApp"
version: "1.0.0"
default_install_dir: "%ProgramFiles%\\MyApp"
vendor: "My Company"
compression_algorithm: "Zstd"
compression_level: 3
```

### Full Configuration Reference

```yaml
application_name: "MyApp"
version: "1.0.0"
default_install_dir: "%ProgramFiles%\\MyApp"
vendor: "My Company"
license_text: "MIT License..."
icon_path: "app.ico"
compression_algorithm: "Zstd"
compression_level: 3
block_size: 4194304
require_admin: true
auto_startup: false
desktop_icons: true
min_windows_version:
  major: 10
  minor: 0
  build: 19041
  process_name: "myapp.exe"
  window:
    width: 600
    height: 450
    min_width: 600
    min_height: 450
    resizable: false
  folder_targets:
    - folder_name: "plugins"
      target_directory: "%AppData%\\MyApp\\plugins"
registry_entries:
  - path: "HKEY_CURRENT_USER\\Software\\MyApp"
    key: "InstallDir"
    value: "%InstallDir%"
    value_type: "String"
ui_resources_dir: "./ui"
flow_file: "./flow.yaml"
script_files:
  - "./scripts/precheck.js"
```

### Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `application_name` | string | required | Display name of the application |
| `version` | string | required | Application version (semver) |
| `default_install_dir` | string | `%ProgramFiles%` | Default installation directory |
| `vendor` | string | optional | Company or developer name |
| `license_text` | string | optional | License agreement text |
| `icon_path` | string | optional | Path to application icon (.ico) |
| `compression_algorithm` | string | `Zstd` | `Zstd` or `Lzma` |
| `compression_level` | number | `3` | 1-22 for Zstd, 0-9 for LZMA |
| `block_size` | number | `4194304` | Compression block size in bytes |
| `require_admin` | boolean | `false` | Require administrator privileges |
| `auto_startup` | boolean | `false` | Add to Windows startup |
| `desktop_icons` | boolean | `false` | Create desktop shortcut |
| `min_windows_version` | object | optional | Minimum Windows version required |
  | `process_name` | string | optional | Process to check before install |
  | `window` | object | optional | Installer window size options |
  | `folder_targets` | array | `[]` | Custom folder mappings |
| `registry_entries` | array | `[]` | Custom registry entries |
| `ui_resources_dir` | string | optional | Custom UI resources directory |
| `flow_file` | string | optional | Install flow YAML to embed into package metadata |
| `script_files` | array | `[]` | Script files to embed for flow `script` steps |

## Flow DSL Reference

If you are building custom install flows with YAML DSL, see:

- `docs/flow_dsl_requirements.md` (requirements)
- `docs/flow_dsl_design.md` (design)
- `docs/flow_dsl_quick_reference.md` (quick reference + common errors)
- `docs/component_extension_design.md` (optional component download/install design)

## UI Customization

The installer GUI uses HTML/CSS/JS for the interface, making it easy to customize.

### UI Resource Structure

```
ui/
├── index.html          # Main HTML file (required)
├── styles/
│   └── main.css        # Stylesheet
├── scripts/
│   └── main.js         # JavaScript logic
└── locales/
    ├── en-US.json      # English translations
    └── zh-CN.json      # Chinese translations
```

### Customizing the UI

1. Copy the default UI resources from `rust-installer/ui/`
2. Modify the HTML, CSS, and JavaScript as needed
3. Specify your custom UI directory in the configuration:

```yaml
ui_resources_dir: "./my-custom-ui"
```

### Tauri Commands Available

The UI can call these Tauri commands:

- `get_metadata(package_path)` - Get package metadata
- `check_prerequisites(package_path, install_dir)` - Check system requirements
- `validate_install_request(request)` - Validate install request (returns structured error codes)
- `start_install(request)` - Start installation
- `cancel_install()` - Cancel ongoing installation
- `browse_directory(default_path)` - Open directory picker
- `get_system_locale()` - Get system language
- `get_translations(locale)` - Get translation strings
- `set_locale(locale)` - Set current language
- `apply_window_config(width, height, min_width, min_height, resizable)` - Apply window size options

### Tauri Events

The UI receives these events:

- `install_progress` - Progress updates during installation
- `install_complete` - Installation completed successfully
- `install_error` - Installation failed
- `install_cancelled` - Installation was cancelled

## Multi-Language Support

### Adding a New Language

1. Create a new JSON file in `ui/locales/` (e.g., `ja-JP.json`)
2. Copy the structure from `en-US.json`
3. Translate all strings
4. Add the language option to the UI selector

### Translation File Format

```json
{
  "welcome.title": "Welcome",
  "welcome.description": "This will install {appName} on your computer.",
  "button.next": "Next",
  "button.cancel": "Cancel"
}
```

Variables use `{variableName}` syntax and are replaced at runtime.

### Supported Languages (Default)

- English (en-US)
- Chinese Simplified (zh-CN)

## Package Format

The installer uses a custom binary format optimized for streaming installation:

```
┌─────────────────────────────────────────┐
│              Header (64 bytes)          │
│  - Magic: "MTI2"                        │
│  - Version, offsets, flags              │
├─────────────────────────────────────────┤
│           Table of Contents             │
│  - File entries (path, size, checksum)  │
│  - Block entries (offset, size, algo)   │
├─────────────────────────────────────────┤
│             Metadata                    │
│  - App info (MessagePack encoded)       │
├─────────────────────────────────────────┤
│            Data Blocks                  │
│  - Compressed file data                 │
├─────────────────────────────────────────┤
│              Footer (48 bytes)          │
│  - Magic: "MTIF"                        │
│  - Quick-access offsets                 │
└─────────────────────────────────────────┘
```

### Format Details

- **Magic Numbers**: `MTI2` (header), `MTIF` (footer)
- **Byte Order**: Little-endian for all numeric fields
- **Checksums**: CRC32 for data integrity
- **Compression**: Zstd (default) or LZMA
- **Metadata Encoding**: MessagePack

## API Documentation

Generate and view the API documentation:

```bash
cargo doc --no-deps --open
```

### Key Types

```rust
// Configuration
use installer_shared::{PackagerConfig, InstallOptions};

// Package format
use installer_core::{PackageHeader, Toc, PackageMetadata};

// Core operations
use installer_core::{Packager, Installer, Uninstaller};

// Progress reporting
use installer_shared::{ProgressEvent, Phase};

// Error handling
use installer_shared::{InstallerError, Result};
```

### Example: Programmatic Usage

```rust
use installer_core::{Packager, PackagerConfig};
use std::path::Path;

fn main() -> installer_shared::Result<()> {
    // Create packager with configuration
    let config = PackagerConfig {
        application_name: "MyApp".to_string(),
        version: "1.0.0".to_string(),
        ..Default::default()
    };
    
    let packager = Packager::new(config)?;
    
    // Build package with progress callback
    let stats = packager.build_package(
        Path::new("./input"),
        Path::new("./output.mti"),
        None, // No custom UI
        |progress| {
            println!("Progress: {:?}", progress);
        },
    )?;
    
    println!("Created package: {} files, {:.2} compression ratio",
        stats.total_files, stats.compression_ratio);
    
    Ok(())
}
```

## Testing

### Running Tests

```bash
# Run all unit tests
cargo test --all

# Run tests with output
cargo test --all -- --nocapture

# Run specific test
cargo test test_package_roundtrip

# Run property-based tests
cargo test --all -- --ignored proptest
```

### Test Categories

- **Unit Tests**: Test individual functions and modules
- **Property Tests**: Verify invariants across random inputs
- **Integration Tests**: Test complete workflows

### Property-Based Testing

The project uses `proptest` for property-based testing. Key properties tested:

1. Package format round-trip consistency
2. Configuration serialization round-trip
3. Block division consistency
4. Checksum integrity verification
5. Parallel compression order preservation

## License

MIT License - see [LICENSE](LICENSE) for details.

---

## Contributing

Contributions are welcome! Please read our contributing guidelines before submitting PRs.

## Support

- **Issues**: [GitHub Issues](https://github.com/example/rust-installer/issues)
- **Documentation**: [API Docs](https://example.github.io/rust-installer/)
