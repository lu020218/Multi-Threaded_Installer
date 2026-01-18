# Build and Deployment Guide

## Overview

This guide provides comprehensive instructions for building and deploying the installer application. It covers development builds, release builds, packaging, and distribution.

## Prerequisites

### Required Software

1. **CMake** (version 3.15 or later)
   - Download: https://cmake.org/download/
   - Add to PATH during installation

2. **Visual Studio 2019 or later**
   - Community Edition (free) or Professional/Enterprise
   - Required workloads:
     - Desktop development with C++
     - Windows 10 SDK

3. **Git** (for version control)
   - Download: https://git-scm.com/

### Optional Tools

1. **Ninja Build System** (faster builds)
   - Download: https://ninja-build.org/
   - Add to PATH

2. **vcpkg** (for dependency management)
   - Clone: https://github.com/Microsoft/vcpkg

## Project Structure

```
project-root/
├── CMakeLists.txt           # Main CMake configuration
├── include/                 # Header files
│   ├── gui/                # GUI headers
│   ├── installer/          # Installer headers
│   └── packager/           # Packager headers
├── src/                    # Source files
│   ├── gui/               # GUI implementation
│   ├── installer/         # Installer implementation
│   └── packager/          # Packager implementation
├── resources/             # UI resources
│   ├── skins/            # XML layouts
│   └── images/           # Image resources
├── third_party/          # Third-party libraries
│   ├── DuiLib_Ultimate/
│   ├── xz/              # LZMA library
│   └── zstd/            # Zstandard library
├── tests/               # Test files
├── docs/                # Documentation
└── build/               # Build output (generated)
```

## Building the Project

### Initial Setup

1. **Clone Repository**
   ```cmd
   git clone <repository-url>
   cd <project-directory>
   ```

2. **Initialize Submodules** (if using)
   ```cmd
   git submodule update --init --recursive
   ```

3. **Create Build Directory**
   ```cmd
   mkdir build
   cd build
   ```

### CMake Configuration

#### Debug Build (Development)

```cmd
cmake .. -G "Visual Studio 16 2019" -A x64 -DBUILD_GUI=ON
```

**Options**:
- `-G "Visual Studio 16 2019"`: Generator (adjust for your VS version)
- `-A x64`: Architecture (x64 or Win32)
- `-DBUILD_GUI=ON`: Enable GUI build (default: ON)
- `-DCMAKE_BUILD_TYPE=Debug`: Debug configuration

#### Release Build (Production)

```cmd
cmake .. -G "Visual Studio 16 2019" -A x64 -DBUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
```

#### Using Ninja (Faster)

```cmd
cmake .. -G "Ninja" -DBUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_GUI` | ON | Build with GUI support |
| `BUILD_TESTS` | ON | Build test suite |
| `CMAKE_BUILD_TYPE` | Debug | Build configuration (Debug/Release) |
| `USE_STATIC_RUNTIME` | ON | Link runtime statically |

**Example with all options**:
```cmd
cmake .. -G "Visual Studio 16 2019" -A x64 ^
  -DBUILD_GUI=ON ^
  -DBUILD_TESTS=ON ^
  -DUSE_STATIC_RUNTIME=ON ^
  -DCMAKE_BUILD_TYPE=Release
```

### Building

#### Using CMake

```cmd
cmake --build . --config Release
```

**Options**:
- `--config Release`: Build configuration (Debug/Release/RelWithDebInfo/MinSizeRel)
- `--parallel 8`: Use 8 parallel jobs (faster)
- `--target installer`: Build specific target only

#### Using Visual Studio

1. Open `build/Installer.sln` in Visual Studio
2. Select configuration (Debug/Release) from toolbar
3. Build → Build Solution (Ctrl+Shift+B)

#### Using MSBuild (Command Line)

```cmd
msbuild Installer.sln /p:Configuration=Release /p:Platform=x64 /m
```

**Options**:
- `/p:Configuration=Release`: Build configuration
- `/p:Platform=x64`: Target platform
- `/m`: Multi-processor build

### Build Output

After successful build:

```
build/
├── Release/                # Release binaries
│   ├── installer.exe      # Main installer executable
│   ├── packager.exe       # Packager tool
│   └── *.dll             # Required DLLs (if any)
├── Debug/                 # Debug binaries
└── tests/                # Test executables
```

## Building Components

### GUI-Enabled Installer

```cmd
cmake .. -DBUILD_GUI=ON
cmake --build . --config Release
```

Output: `build/Release/installer.exe` (with GUI)

### Console-Only Installer

```cmd
cmake .. -DBUILD_GUI=OFF
cmake --build . --config Release
```

Output: `build/Release/installer.exe` (console only, smaller size)

### Packager Tool

```cmd
cmake --build . --config Release --target packager
```

Output: `build/Release/packager.exe`

### Tests

```cmd
cmake --build . --config Release --target tests
```

Output: `build/Release/tests/*.exe`

## Running Tests

### All Tests

```cmd
cd build
ctest -C Release
```

Or using test runner scripts:

**Windows Batch**:
```cmd
cd tests
run_all_tests.bat
```

**PowerShell**:
```powershell
cd tests
.\run_all_tests.ps1
```

### Specific Test

```cmd
cd build/Release
test_gui_helpers.exe
```

### Test with Verbose Output

```cmd
ctest -C Release -V
```

## Packaging for Distribution

### Step 1: Build Release Version

```cmd
mkdir build-release
cd build-release
cmake .. -G "Visual Studio 16 2019" -A x64 -DBUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Step 2: Collect Required Files

Create distribution directory:

```cmd
mkdir dist
cd dist
```

Copy files:

```cmd
REM Main executable
copy ..\build-release\Release\installer.exe .

REM Resources
xcopy /E /I ..\resources resources

REM Documentation (optional)
copy ..\README.md .
copy ..\LICENSE .
```

### Step 3: Verify Dependencies

Check for DLL dependencies:

```cmd
dumpbin /dependents installer.exe
```

If any DLLs are required:
- Copy them to dist folder
- Or use static linking (recommended)

### Step 4: Test Standalone

Test installer runs without development environment:

```cmd
cd dist
installer.exe
```

Verify:
- [ ] Installer launches
- [ ] GUI displays correctly
- [ ] All images load
- [ ] Installation completes successfully

### Step 5: Create Installer Package

#### Option A: ZIP Archive

```cmd
cd dist
powershell Compress-Archive -Path * -DestinationPath ..\MyApp-Installer-v1.0.0.zip
```

#### Option B: Self-Extracting Archive

Use 7-Zip or WinRAR to create SFX:

```cmd
7z a -sfx installer-package.exe *
```

#### Option C: NSIS Installer (Installer for the Installer)

Create NSIS script (`installer-setup.nsi`):

```nsis
!define APP_NAME "MyApp Installer"
!define APP_VERSION "1.0.0"

Name "${APP_NAME}"
OutFile "MyApp-Setup-${APP_VERSION}.exe"
InstallDir "$TEMP\MyAppInstaller"

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "dist\*.*"
    ExecWait "$INSTDIR\installer.exe"
    Delete "$INSTDIR\*.*"
    RMDir "$INSTDIR"
SectionEnd
```

Compile:
```cmd
makensis installer-setup.nsi
```

## Static Linking

To create standalone executable without DLL dependencies:

### CMake Configuration

```cmd
cmake .. -DUSE_STATIC_RUNTIME=ON -DCMAKE_BUILD_TYPE=Release
```

This sets:
- `/MT` flag (static runtime)
- Static linking of libraries

### Verify Static Linking

```cmd
dumpbin /dependents installer.exe
```

Should only show system DLLs:
- KERNEL32.dll
- USER32.dll
- GDI32.dll
- etc.

No MSVC runtime DLLs (msvcp140.dll, vcruntime140.dll).

## Optimization

### Release Build Optimizations

CMake automatically applies:
- `/O2`: Maximize speed
- `/GL`: Whole program optimization
- `/LTCG`: Link-time code generation

### Size Optimization

For smaller executable:

```cmd
cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel
```

Applies:
- `/O1`: Minimize size
- `/Os`: Favor small code

### Additional Optimizations

Edit `CMakeLists.txt`:

```cmake
if(MSVC)
    add_compile_options(/O2 /Oi /Ot /GL)
    add_link_options(/LTCG /OPT:REF /OPT:ICF)
endif()
```

## Code Signing

### Why Sign Code?

- Prevents "Unknown Publisher" warnings
- Builds user trust
- Required for some enterprise deployments
- Prevents tampering

### Obtaining Certificate

1. **Purchase from CA**
   - DigiCert, Sectigo, GlobalSign, etc.
   - Code signing certificate (~$100-500/year)

2. **Self-Signed** (Testing Only)
   ```cmd
   makecert -r -pe -n "CN=MyCompany" -ss My -sr CurrentUser ^
     -a sha256 -cy end -sky signature -sv MyKey.pvk MyCert.cer
   ```

### Signing Executable

Using `signtool.exe` (from Windows SDK):

```cmd
signtool sign /f MyCert.pfx /p password /t http://timestamp.digicert.com installer.exe
```

**Options**:
- `/f`: Certificate file
- `/p`: Certificate password
- `/t`: Timestamp server (important!)
- `/d`: Description
- `/du`: URL

**With description**:
```cmd
signtool sign /f MyCert.pfx /p password ^
  /t http://timestamp.digicert.com ^
  /d "MyApp Installer" ^
  /du "https://myapp.com" ^
  installer.exe
```

### Verify Signature

```cmd
signtool verify /pa installer.exe
```

Or right-click installer → Properties → Digital Signatures tab.

## Continuous Integration

### GitHub Actions

Create `.github/workflows/build.yml`:

```yaml
name: Build Installer

on:
  push:
    branches: [ main ]
  pull_request:
    branches: [ main ]

jobs:
  build:
    runs-on: windows-latest
    
    steps:
    - uses: actions/checkout@v2
      with:
        submodules: recursive
    
    - name: Setup CMake
      uses: lukka/get-cmake@latest
    
    - name: Configure
      run: cmake -B build -G "Visual Studio 16 2019" -A x64 -DBUILD_GUI=ON
    
    - name: Build
      run: cmake --build build --config Release
    
    - name: Test
      run: ctest --test-dir build -C Release
    
    - name: Upload Artifact
      uses: actions/upload-artifact@v2
      with:
        name: installer
        path: build/Release/installer.exe
```

### Azure Pipelines

Create `azure-pipelines.yml`:

```yaml
trigger:
- main

pool:
  vmImage: 'windows-latest'

steps:
- task: CMake@1
  inputs:
    workingDirectory: 'build'
    cmakeArgs: '.. -G "Visual Studio 16 2019" -A x64 -DBUILD_GUI=ON'

- task: CMake@1
  inputs:
    workingDirectory: 'build'
    cmakeArgs: '--build . --config Release'

- task: CmdLine@2
  inputs:
    script: 'ctest -C Release'
    workingDirectory: 'build'

- task: PublishBuildArtifacts@1
  inputs:
    pathToPublish: 'build/Release/installer.exe'
    artifactName: 'installer'
```

## Deployment Strategies

### Direct Download

1. Build release version
2. Sign executable
3. Upload to website
4. Provide download link

**Advantages**:
- Simple
- Direct control
- No third-party dependencies

### CDN Distribution

1. Upload to CDN (CloudFlare, AWS CloudFront, etc.)
2. Provide CDN URL
3. Benefits from caching and global distribution

**Example** (AWS S3 + CloudFront):
```cmd
aws s3 cp installer.exe s3://mybucket/downloads/installer-v1.0.0.exe
aws cloudfront create-invalidation --distribution-id DISTID --paths "/downloads/*"
```

### Package Managers

#### Chocolatey

Create `myapp-installer.nuspec`:

```xml
<?xml version="1.0"?>
<package>
  <metadata>
    <id>myapp-installer</id>
    <version>1.0.0</version>
    <title>MyApp Installer</title>
    <authors>MyCompany</authors>
    <description>Installer for MyApp</description>
  </metadata>
  <files>
    <file src="installer.exe" target="tools" />
  </files>
</package>
```

Package and publish:
```cmd
choco pack
choco push myapp-installer.1.0.0.nupkg --source https://push.chocolatey.org/
```

#### Winget

Create manifest and submit to winget-pkgs repository.

### Enterprise Deployment

#### Group Policy

1. Place installer on network share
2. Create GPO for software installation
3. Deploy to target computers

#### SCCM/ConfigMgr

1. Create application in ConfigMgr
2. Configure detection method
3. Deploy to collections

#### Intune

1. Package as Win32 app (.intunewin)
2. Upload to Intune
3. Configure deployment settings
4. Assign to groups

## Version Management

### Versioning Scheme

Use Semantic Versioning (SemVer):
- **MAJOR.MINOR.PATCH** (e.g., 1.0.0)
- MAJOR: Breaking changes
- MINOR: New features (backward compatible)
- PATCH: Bug fixes

### Setting Version

Edit `CMakeLists.txt`:

```cmake
project(Installer VERSION 1.0.0)
```

Or pass as CMake variable:

```cmd
cmake .. -DPROJECT_VERSION=1.0.0
```

### Version in Executable

Add version resource to `installer.rc`:

```rc
VS_VERSION_INFO VERSIONINFO
 FILEVERSION 1,0,0,0
 PRODUCTVERSION 1,0,0,0
 FILEFLAGSMASK 0x3fL
 FILEFLAGS 0x0L
 FILEOS 0x40004L
 FILETYPE 0x1L
 FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName", "MyCompany"
            VALUE "FileDescription", "MyApp Installer"
            VALUE "FileVersion", "1.0.0.0"
            VALUE "ProductName", "MyApp"
            VALUE "ProductVersion", "1.0.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
```

## Troubleshooting Build Issues

### CMake Configuration Fails

**Error**: "CMake Error: Could not find CMAKE_ROOT"

**Solution**:
- Verify CMake installed correctly
- Add CMake to PATH
- Restart command prompt

**Error**: "Could not find Visual Studio"

**Solution**:
- Install Visual Studio with C++ workload
- Specify generator explicitly: `-G "Visual Studio 16 2019"`

### Build Fails

**Error**: "Cannot open include file"

**Solution**:
- Verify all dependencies present
- Check include paths in CMakeLists.txt
- Update submodules: `git submodule update --init`

**Error**: "Unresolved external symbol"

**Solution**:
- Check library linking in CMakeLists.txt
- Verify library files exist
- Rebuild dependencies

### Resource Files Not Copied

**Error**: Images/XML files not found at runtime

**Solution**:
- Check POST_BUILD commands in CMakeLists.txt
- Manually copy resources to build directory
- Verify resource paths in code

### DLL Dependencies

**Error**: "MSVCP140.dll not found"

**Solution**:
- Use static runtime: `-DUSE_STATIC_RUNTIME=ON`
- Or distribute Visual C++ Redistributable
- Or copy required DLLs to installer directory

## Best Practices

### Development

1. **Use Debug builds for development**
   - Faster compilation
   - Better debugging information
   - Assertions enabled

2. **Use Release builds for testing**
   - Performance testing
   - Size optimization
   - Final validation

3. **Keep build directory separate**
   - Don't commit build artifacts
   - Add `build/` to `.gitignore`
   - Use out-of-source builds

### Release

1. **Always build from clean state**
   ```cmd
   rmdir /s /q build
   mkdir build
   cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build . --config Release
   ```

2. **Run all tests before release**
   ```cmd
   ctest -C Release
   ```

3. **Sign all executables**
   - Use trusted certificate
   - Include timestamp

4. **Document build process**
   - Version numbers
   - Build date
   - Commit hash

5. **Create reproducible builds**
   - Document exact tool versions
   - Use version control tags
   - Archive build environment

## Automation Scripts

### Build Script (PowerShell)

Create `build.ps1`:

```powershell
param(
    [string]$Config = "Release",
    [switch]$Clean
)

if ($Clean) {
    Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force -Path build | Out-Null
Set-Location build

cmake .. -G "Visual Studio 16 2019" -A x64 -DBUILD_GUI=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build . --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Build completed successfully!" -ForegroundColor Green
```

Usage:
```powershell
.\build.ps1 -Config Release -Clean
```

### Package Script

Create `package.ps1`:

```powershell
param(
    [string]$Version = "1.0.0"
)

$DistDir = "dist-$Version"

# Clean and create dist directory
Remove-Item -Recurse -Force $DistDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

# Copy files
Copy-Item "build\Release\installer.exe" $DistDir
Copy-Item -Recurse "resources" $DistDir
Copy-Item "README.md" $DistDir
Copy-Item "LICENSE" $DistDir

# Create ZIP
$ZipFile = "MyApp-Installer-v$Version.zip"
Compress-Archive -Path "$DistDir\*" -DestinationPath $ZipFile -Force

Write-Host "Package created: $ZipFile" -ForegroundColor Green
```

Usage:
```powershell
.\package.ps1 -Version "1.0.0"
```

## Support

For build and deployment issues:
1. Check this guide
2. Review CMakeLists.txt
3. Verify tool versions
4. Check build logs
5. Contact development team with:
   - CMake version
   - Visual Studio version
   - Error messages
   - Build log

## Additional Resources

- CMake Documentation: https://cmake.org/documentation/
- Visual Studio Documentation: https://docs.microsoft.com/visualstudio/
- Code Signing Guide: https://docs.microsoft.com/windows/win32/seccrypto/cryptography-tools
- Windows SDK: https://developer.microsoft.com/windows/downloads/windows-sdk/
