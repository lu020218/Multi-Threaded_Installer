# Command Line Reference

## Overview

This project has two executables:

- `packager.exe`: build installer packages
- `installer.exe`: run install/uninstall

## packager.exe

### Syntax

```cmd
packager.exe [options] <input_directory> <output_file>
```

### Options

- `-a`, `--algorithm <lzma|zstd>`
  - Compression algorithm.
  - Default: `lzma`.
- `-l`, `--level <level>`
  - Compression level.
  - `lzma`: `0-9`
  - `zstd`: `1-22`
  - If not provided, runtime default is used (`lzma=9`, `zstd=3`).
- `-p`, `--data-out <file>`
  - Also write external data package (`.dat`).
- `-t`, `--threads <count>`
  - Compression thread count.
- `-v`, `--verbose`
  - Verbose logging.
- `-h`, `--help`
  - Show help.

### Behavior Notes

- If config file provides `compressionAlgorithm` / `compressionLevel`, they are used by default.
- If CLI `--algorithm` is provided, it overrides config algorithm.
- If CLI `--level` is provided, it overrides config level.

### Examples

```cmd
packager.exe .\input .\dist\Setup.exe
packager.exe -a lzma -l 9 .\input .\dist\Setup.exe
packager.exe -a zstd -l 3 .\input .\dist\Setup.exe
packager.exe -a zstd -l 5 -p .\dist\payload.dat .\input .\dist\Setup.exe
```

## installer.exe

### Syntax

```cmd
installer.exe [options] [folder_mappings...]
```

`folder_mappings` format:

```text
<folder_name>=<target_path>
```

### Options

- `-d`, `--destination <directory>`
  - Default installation directory.
- `-p`, `--data-package <file>`
  - Use external data package.
- `-t`, `--threads <count>`
  - Decompression thread count.
- `-f`, `--force`
  - Force overwrite.
- `-s`, `--silent`
  - Silent mode.
- `--debug`
  - Show console in GUI build.
- `--uninstall`
  - Uninstall mode.
- `--component <id>`
  - Select one component (repeatable).
- `--components <id1,id2,...>`
  - Select multiple components.
- `--all-components`
  - Install all optional components.
- `-v`, `--verbose`
  - Verbose logging.
- `-h`, `--help`
  - Show help.

### Component Selection Rules

- No component flags: install `required + defaultSelected`.
- With explicit component flags: install `required + selected + dependency closure`.
- Unknown component ID: installation fails with clear error.

### Uninstall Behavior

- `--uninstall` enters uninstall mode.
- If executable name is `uninstall.exe`, uninstall mode is auto-detected.

### Examples

```cmd
installer.exe
installer.exe -s
installer.exe --uninstall
installer.exe -s --components core,plugins
installer.exe --component core --component tools
installer.exe -d "C:\Program Files\MyApp"
```

## Exit Codes

Installer exit codes are defined in `src/common/installer_exit_codes.h`.
