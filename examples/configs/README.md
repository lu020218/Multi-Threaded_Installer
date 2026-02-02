# Example Configurations

This directory currently contains legacy JSON examples for historical reference.
New packager versions are YAML-only.

Use `examples/yaml-flow-demo/packager.yaml` as the primary template.

## Files

### basic.json

A minimal configuration for simple applications:
- Basic app name and version
- Default compression settings (Zstd level 3)
- Desktop shortcut creation

Use this as a starting point for simple installers.

### advanced.json

A comprehensive configuration demonstrating all available options:
- Custom installation directory with company subfolder
- Full license text
- High compression level (Zstd level 9)
- Larger block size (8MB) for better compression ratio
- Administrator privileges required
- Minimum Windows version check
- Process detection before installation
- Custom folder targets for plugins and config
- Multiple registry entries (String and DWORD types)
- Custom UI resources
- Thread count configuration

Use this as a reference for all available configuration options.

### multilingual.json

A configuration optimized for multi-language applications:
- Auto-startup enabled
- Language preference stored in registry
- Custom UI resources directory for localized interface
- Moderate compression for balance between size and speed

Use this when building installers with multi-language support.

## Usage (YAML)

Copy a YAML config to your project directory and name it `packager.yaml`:

```bash
cp ../yaml-flow-demo/packager.yaml /path/to/your/project/packager.yaml
```

Then run the packager:

```bash
packager --input /path/to/your/project --output installer.mti
```

## Configuration Reference

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `application_name` | string | Yes | Display name of the application |
| `version` | string | Yes | Application version (semver format) |
| `default_install_dir` | string | Yes | Default installation path (supports env vars) |
| `vendor` | string | No | Company or developer name |
| `license_text` | string | No | License agreement text |
| `icon_path` | string | No | Path to application icon (.ico) |
| `compression_algorithm` | string | No | "Zstd" (default) or "Lzma" |
| `compression_level` | number | No | 1-22 for Zstd, 0-9 for LZMA |
| `block_size` | number | No | Block size in bytes (default: 4MB) |
| `require_admin` | boolean | No | Require administrator privileges |
| `auto_startup` | boolean | No | Add to Windows startup |
| `desktop_icons` | boolean | No | Create desktop shortcut |
| `min_windows_version` | object | No | Minimum Windows version required |
| `process_name` | string | No | Process to check before install |
| `folder_targets` | array | No | Custom folder mappings |
| `registry_entries` | array | No | Custom registry entries |
| `ui_resources_dir` | string | No | Custom UI resources directory |
| `thread_count` | number | No | Thread count for parallel operations |

## Environment Variables

The following environment variables can be used in paths:

- `%ProgramFiles%` - Program Files directory
- `%ProgramFiles(x86)%` - Program Files (x86) directory
- `%AppData%` - User's roaming application data
- `%LocalAppData%` - User's local application data
- `%UserProfile%` - User's profile directory
- `%InstallDir%` - Installation directory (for registry values)
