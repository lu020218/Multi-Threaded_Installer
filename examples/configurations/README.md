# Configuration Examples

This directory contains example configuration files for various common scenarios.

## Available Examples

1. **basic.json** - Minimal configuration for simple applications
2. **desktop-app.json** - Standard desktop application with program files
3. **user-data-app.json** - Application with user-specific data folders
4. **multi-user-app.json** - Multi-user application with shared and user-specific data
5. **plugin-system.json** - Application with plugin architecture
6. **portable-app.json** - Portable application configuration
7. **high-compression.json** - Configuration optimized for download size
8. **advanced.json** - Advanced configuration with file mappings

## How to Use These Examples

1. Copy the example that best matches your needs
2. Rename it to `packager.json`
3. Place it in your input directory
4. Modify the values to match your application
5. Run the packager: `packager.exe <input_directory> <output_file>`

## Quick Reference

### Basic Configuration

For simple applications that install everything to one location:

```json
{
  "applicationName": "MyApp"
}
```

### With Compression Choice

```json
{
  "applicationName": "MyApp",
  "compressionAlgorithm": "lzma"
}
```

### With Folder Targets

```json
{
  "applicationName": "MyApp",
  "folderTargets": [
    {
      "folder": "app",
      "targetDirectory": "installDirectory"
    },
    {
      "folder": "data",
      "targetDirectory": "%AppData%\\Roaming"
    }
  ]
}
```

## Need Help?

- See [Configuration Reference](../../docs/configuration_reference.md) for complete documentation
- See [Migration Guide](../../docs/migration_guide.md) for migrating from command-line arguments
