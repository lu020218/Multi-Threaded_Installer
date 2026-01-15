# Multi-Threaded Installer

A C++ packager and installer system that creates self-extracting installers with multi-threaded decompression capabilities.

## Project Structure

```
├── CMakeLists.txt              # Build configuration
├── README.md                   # This file
├── include/                    # Header files
│   ├── common/
│   │   └── types.h            # Core data structures and enums
│   ├── packager/              # Packager component headers
│   │   ├── folder_scanner.h
│   │   ├── compression_module.h
│   │   ├── metadata_generator.h
│   │   └── installer_generator.h
│   └── installer/             # Installer component headers
│       ├── metadata_parser.h
│       ├── thread_pool_manager.h
│       ├── decompression_engine.h
│       ├── file_system_operator.h
│       └── console_interface.h
├── src/                       # Source files
│   ├── common/
│   │   └── types.cpp
│   ├── packager/              # Packager implementation
│   │   ├── main.cpp
│   │   ├── folder_scanner.cpp
│   │   ├── compression_module.cpp
│   │   ├── metadata_generator.cpp
│   │   └── installer_generator.cpp
│   └── installer/             # Installer implementation
│       ├── main.cpp
│       ├── metadata_parser.cpp
│       ├── thread_pool_manager.cpp
│       ├── decompression_engine.cpp
│       ├── file_system_operator.cpp
│       └── console_interface.cpp
└── tests/                     # Test files
    ├── test_main.cpp          # Unit test runner
    ├── test_*.cpp             # Unit test files
    └── pbt/                   # Property-based tests
        └── test_compression_roundtrip.cpp
```

## Dependencies

- **7z SDK / liblzma** (>= 19.00): LZMA compression support
- **RapidCheck**: Property-based testing framework (for tests)
- **CMake** (>= 3.16): Build system
- **C++17** compatible compiler

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

### Packager
```bash
./packager [options] <input_directory> <output_file>

Options:
  -a, --algorithm <lzma>         Choose compression algorithm (default: lzma)
  -l, --level <level>            Compression level (lzma: 0-9)
  -t, --threads <count>          Number of compression threads (default: CPU cores)
  -v, --verbose                  Show detailed information
  -h, --help                     Show help message
```

### Installer
```bash
./installer [options] [folder_mappings...]

Options:
  -d, --destination <directory>  Default installation directory
  -t, --threads <count>          Number of decompression threads (default: CPU cores)
  -f, --force                    Force overwrite existing files
  -s, --silent                   Silent installation mode
  -v, --verbose                  Show detailed information
  -h, --help                     Show help message
```

## Testing

```bash
# Run unit tests
./tests

# Run property-based tests (if RapidCheck is available)
./pbt_tests
```

## Features

- **Multi-threaded compression and decompression**
- **Single compression algorithm** (LZMA)
- **Self-extracting installers**
- **Cross-platform support** (Windows, Linux, macOS)
- **Command-line and interactive interfaces**
- **Comprehensive testing** (unit tests + property-based tests)

## Architecture

The system consists of two main components:

1. **Packager**: Scans input directories, compresses folders, generates metadata, and creates self-extracting installers
2. **Installer**: Parses embedded metadata, decompresses folders using multiple threads, and installs files to target directories

Both components share common data structures and use a modular design for maintainability and extensibility.
