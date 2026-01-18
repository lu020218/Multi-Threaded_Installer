# Release Quick Start Guide

## Overview

This guide provides a quick reference for building and releasing the installer. For detailed information, see the full documentation.

## Prerequisites

- Visual Studio 2019 or later
- CMake 3.15+
- Git
- PowerShell (for automated scripts)

## Quick Release Process

### 1. Prepare for Release

```powershell
# Update version number in CMakeLists.txt
# Update CHANGELOG.md
# Commit all changes
git add .
git commit -m "Prepare for release v1.0.0"
git push
```

### 2. Run Release Script

**PowerShell** (Recommended):
```powershell
.\scripts\prepare_release.ps1 -Version "1.0.0" -Clean
```

**Batch**:
```cmd
scripts\prepare_release.bat
```

### 3. Test the Build

```cmd
cd dist-v1.0.0
installer.exe
```

Test checklist:
- [ ] Installer launches
- [ ] GUI displays correctly
- [ ] Installation completes successfully
- [ ] Silent mode works: `installer.exe -s`

### 4. Sign the Executable (Optional but Recommended)

```cmd
signtool sign /f cert.pfx /p password /t http://timestamp.digicert.com installer.exe
```

### 5. Create Release Tag

```cmd
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0
```

### 6. Distribute

- Upload `Installer-v1.0.0-<timestamp>.zip` to distribution server
- Update download links
- Publish release notes

## Manual Build Process

If you prefer manual control:

### 1. Configure

```cmd
mkdir build-release
cd build-release
cmake .. -G "Visual Studio 16 2019" -A x64 -DBUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
```

### 2. Build

```cmd
cmake --build . --config Release --parallel
```

### 3. Test

```cmd
ctest -C Release
```

### 4. Package

```cmd
mkdir ..\dist
copy Release\installer.exe ..\dist\
xcopy /E /I ..\resources ..\dist\resources
```

## Common Issues

### Build Fails

**Problem**: CMake configuration fails

**Solution**:
- Verify Visual Studio installed with C++ workload
- Check CMake version: `cmake --version`
- Try specifying generator explicitly

### Tests Fail

**Problem**: Some tests fail

**Solution**:
- Review test output
- Check if GUI resources are present
- Verify DuiLib library is available

### Resources Not Copied

**Problem**: Images/XML files missing

**Solution**:
- Check POST_BUILD commands in CMakeLists.txt
- Manually copy resources directory
- Verify paths in build script

## Quick Commands Reference

### Build Commands

```cmd
# Clean build
cmake --build build-release --config Release --target clean

# Build specific target
cmake --build build-release --config Release --target installer

# Parallel build (faster)
cmake --build build-release --config Release --parallel 8
```

### Test Commands

```cmd
# Run all tests
ctest -C Release

# Run specific test
build-release\Release\test_gui_helpers.exe

# Verbose test output
ctest -C Release -V
```

### Package Commands

```powershell
# Create ZIP archive
Compress-Archive -Path dist-v1.0.0\* -DestinationPath Installer-v1.0.0.zip

# Calculate checksum
Get-FileHash installer.exe -Algorithm SHA256
```

## File Locations

After successful build:

```
project-root/
├── build-release/          # Build directory
│   └── Release/
│       ├── installer.exe   # Built executable
│       └── packager.exe    # Packager tool
├── dist-v1.0.0/           # Distribution package
│   ├── installer.exe      # Installer
│   ├── resources/         # UI resources
│   └── docs/              # Documentation
└── Installer-v1.0.0-<timestamp>.zip  # Release archive
```

## Version Numbering

Use Semantic Versioning (SemVer):

- **MAJOR.MINOR.PATCH** (e.g., 1.0.0)
- **MAJOR**: Breaking changes
- **MINOR**: New features (backward compatible)
- **PATCH**: Bug fixes

Examples:
- `1.0.0` - Initial release
- `1.0.1` - Bug fix release
- `1.1.0` - New features added
- `2.0.0` - Breaking changes

## Release Checklist (Abbreviated)

- [ ] Version updated in CMakeLists.txt
- [ ] CHANGELOG.md updated
- [ ] All tests passing
- [ ] Build successful
- [ ] Manual testing completed
- [ ] Executable signed (if applicable)
- [ ] Release tag created
- [ ] Archive uploaded
- [ ] Release notes published

## Support

For detailed information:
- Full build guide: `docs/BUILD_AND_DEPLOYMENT.md`
- Release checklist: `docs/RELEASE_CHECKLIST.md`
- Troubleshooting: `docs/TROUBLESHOOTING.md`

## Quick Tips

1. **Always build from clean state for releases**
   ```powershell
   .\scripts\prepare_release.ps1 -Clean
   ```

2. **Test on clean Windows VM**
   - Ensures no missing dependencies
   - Verifies standalone operation

3. **Use static runtime linking**
   - Already enabled in release script
   - Reduces DLL dependencies

4. **Sign executables**
   - Prevents "Unknown Publisher" warnings
   - Builds user trust

5. **Document everything**
   - Update CHANGELOG.md
   - Create release notes
   - Archive build information

## Emergency Hotfix Process

For critical bugs in released version:

1. **Create hotfix branch**
   ```cmd
   git checkout -b hotfix-1.0.1 v1.0.0
   ```

2. **Fix the issue**
   ```cmd
   # Make changes
   git commit -m "Fix critical bug"
   ```

3. **Build and test**
   ```powershell
   .\scripts\prepare_release.ps1 -Version "1.0.1" -Clean
   ```

4. **Release hotfix**
   ```cmd
   git tag -a v1.0.1 -m "Hotfix release 1.0.1"
   git push origin v1.0.1
   ```

5. **Merge back to main**
   ```cmd
   git checkout main
   git merge hotfix-1.0.1
   git push
   ```

## Automation

For CI/CD integration, see:
- GitHub Actions example in `docs/BUILD_AND_DEPLOYMENT.md`
- Azure Pipelines example in `docs/BUILD_AND_DEPLOYMENT.md`

## Next Steps

After successful release:
1. Monitor download statistics
2. Watch for error reports
3. Gather user feedback
4. Plan next release
5. Update roadmap

---

**Remember**: Quality is more important than speed. Take time to test thoroughly before releasing.
