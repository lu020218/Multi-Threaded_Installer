# Multi-Threaded Installer

A C++ packager and installer system that creates self-extracting installers with multi-threaded decompression capabilities.

## Project Structure

```
鈹溾攢鈹€ CMakeLists.txt              # Build configuration
鈹溾攢鈹€ README.md                   # This file
鈹溾攢鈹€ include/                    # Header files
鈹?  鈹溾攢鈹€ common/
鈹?  鈹?  鈹斺攢鈹€ types.h            # Core data structures and enums
鈹?  鈹溾攢鈹€ packager/              # Packager component headers
鈹?  鈹?  鈹溾攢鈹€ folder_scanner.h
鈹?  鈹?  鈹溾攢鈹€ compression_module.h
鈹?  鈹?  鈹溾攢鈹€ metadata_generator.h
鈹?  鈹?  鈹斺攢鈹€ installer_generator.h
鈹?  鈹斺攢鈹€ installer/             # Installer component headers
鈹?      鈹溾攢鈹€ metadata_parser.h
鈹?      鈹溾攢鈹€ thread_pool_manager.h
鈹?      鈹溾攢鈹€ decompression_engine.h
鈹?      鈹溾攢鈹€ file_system_operator.h
鈹?      鈹斺攢鈹€ console_interface.h
鈹溾攢鈹€ src/                       # Source files
鈹?  鈹溾攢鈹€ common/
鈹?  鈹?  鈹斺攢鈹€ types.cpp
鈹?  鈹溾攢鈹€ packager/              # Packager implementation
鈹?  鈹?  鈹溾攢鈹€ main.cpp
鈹?  鈹?  鈹溾攢鈹€ folder_scanner.cpp
鈹?  鈹?  鈹溾攢鈹€ compression_module.cpp
鈹?  鈹?  鈹溾攢鈹€ metadata_generator.cpp
鈹?  鈹?  鈹斺攢鈹€ installer_generator.cpp
鈹?  鈹斺攢鈹€ installer/             # Installer implementation
鈹?      鈹溾攢鈹€ main.cpp
鈹?      鈹溾攢鈹€ metadata_parser.cpp
鈹?      鈹溾攢鈹€ thread_pool_manager.cpp
鈹?      鈹溾攢鈹€ decompression_engine.cpp
鈹?      鈹溾攢鈹€ file_system_operator.cpp
鈹?      鈹斺攢鈹€ console_interface.cpp
鈹斺攢鈹€ tests/                     # Test files
    鈹溾攢鈹€ test_main.cpp          # Unit test runner
    鈹溾攢鈹€ test_*.cpp             # Unit test files
    鈹斺攢鈹€ pbt/                   # Property-based tests
        鈹斺攢鈹€ test_compression_roundtrip.cpp
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
  -p, --data-out <file>          Write external data package
  -t, --threads <count>          Number of compression threads (default: CPU cores)
  -v, --verbose                  Show detailed information
  -h, --help                     Show help message
```

### Installer
```bash
./installer [options] [folder_mappings...]

Options:
  -d, --destination <directory>  Default installation directory
  -p, --data-package <file>      Use external data package
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


