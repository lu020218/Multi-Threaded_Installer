# Release Checklist

## Overview

This checklist ensures all necessary steps are completed before releasing a new version of the installer. Follow this checklist for every release to maintain quality and consistency.

## Pre-Release Checklist

### Code Quality

- [ ] All code changes committed to version control
- [ ] No debug code or commented-out code in release
- [ ] All compiler warnings resolved
- [ ] Code reviewed by at least one other developer
- [ ] No hardcoded paths or credentials
- [ ] All TODOs and FIXMEs addressed or documented

### Version Information

- [ ] Version number updated in `CMakeLists.txt`
- [ ] Version number updated in documentation
- [ ] Version number updated in installer manifest
- [ ] CHANGELOG.md updated with release notes
- [ ] Release date set in documentation

### Testing

- [ ] All unit tests passing
- [ ] All integration tests passing
- [ ] GUI tests completed successfully
- [ ] Manual testing completed on target platforms
- [ ] Tested on clean Windows installation
- [ ] Tested with different DPI settings (100%, 125%, 150%, 200%)
- [ ] Tested on different Windows versions:
  - [ ] Windows 7
  - [ ] Windows 8/8.1
  - [ ] Windows 10
  - [ ] Windows 11
- [ ] Silent mode installation tested
- [ ] Uninstallation tested
- [ ] Upgrade from previous version tested (if applicable)

### Build Configuration

- [ ] Release build configuration verified
- [ ] Static runtime linking enabled
- [ ] Optimization flags set correctly
- [ ] Debug symbols removed or separated
- [ ] Build reproducible from clean state
- [ ] No build warnings or errors

### Resources

- [ ] All XML layout files present and valid
- [ ] All image resources present and optimized
- [ ] License text file included and up-to-date
- [ ] Application logo updated (if changed)
- [ ] All resources copied to output directory

### Documentation

- [ ] User guide reviewed and updated
- [ ] Command-line reference updated
- [ ] Troubleshooting guide updated
- [ ] Developer documentation updated
- [ ] README.md updated
- [ ] LICENSE file present and correct
- [ ] Release notes prepared

### Security

- [ ] No known security vulnerabilities
- [ ] Dependencies updated to latest secure versions
- [ ] Code signing certificate valid and not expired
- [ ] Executable signed with valid certificate
- [ ] Timestamp server used for signature
- [ ] Signature verified on signed executable

### Compliance

- [ ] License compliance verified for all dependencies
- [ ] Third-party licenses included in distribution
- [ ] Copyright notices updated
- [ ] Privacy policy reviewed (if applicable)
- [ ] Terms of service reviewed (if applicable)

## Build Process

### Preparation

- [ ] Clean workspace (no uncommitted changes)
- [ ] Latest code pulled from repository
- [ ] Submodules updated (if applicable)
- [ ] Build environment verified (correct tool versions)

### Build Steps

- [ ] Run release preparation script:
  ```powershell
  .\scripts\prepare_release.ps1 -Version "X.Y.Z" -Clean
  ```
  Or:
  ```cmd
  scripts\prepare_release.bat
  ```

- [ ] Verify build completed without errors
- [ ] Check build output directory
- [ ] Verify all required files present

### Post-Build Verification

- [ ] Installer executable present
- [ ] Packager executable present (if included)
- [ ] Resources directory copied correctly
- [ ] Documentation files included
- [ ] Required DLLs included (if any)
- [ ] File sizes reasonable (not bloated)

## Testing Release Build

### Functional Testing

- [ ] Launch installer (GUI mode)
- [ ] Verify welcome page displays correctly
- [ ] Test path selection and browsing
- [ ] Test license agreement dialog
- [ ] Verify disk space check works
- [ ] Test installation process
- [ ] Verify progress updates correctly
- [ ] Test cancellation during installation
- [ ] Verify completion page displays correctly
- [ ] Test "Run Application" option (if applicable)
- [ ] Test "Open Webpage" option (if applicable)

### Silent Mode Testing

- [ ] Run silent installation:
  ```cmd
  installer.exe -s
  ```
- [ ] Verify exit code is 0 on success
- [ ] Verify console output is appropriate
- [ ] Verify files installed correctly
- [ ] Test silent installation failure scenarios

### Compatibility Testing

- [ ] Test on 32-bit Windows (if supported)
- [ ] Test on 64-bit Windows
- [ ] Test with different user permissions:
  - [ ] Administrator
  - [ ] Standard user
  - [ ] Limited user
- [ ] Test with antivirus enabled
- [ ] Test with Windows Defender enabled
- [ ] Test with UAC at different levels

### Stress Testing

- [ ] Test with low disk space
- [ ] Test with slow disk (USB 2.0)
- [ ] Test with network drive
- [ ] Test with long file paths
- [ ] Test with special characters in path
- [ ] Test with multiple simultaneous installations

### Uninstallation Testing

- [ ] Verify uninstaller present after installation
- [ ] Test uninstallation process
- [ ] Verify files removed correctly
- [ ] Verify registry entries cleaned up
- [ ] Verify shortcuts removed
- [ ] Check for leftover files

## Code Signing

### Certificate Verification

- [ ] Code signing certificate valid
- [ ] Certificate not expired
- [ ] Certificate from trusted CA
- [ ] Certificate matches company name

### Signing Process

- [ ] Sign installer executable:
  ```cmd
  signtool sign /f cert.pfx /p password /t http://timestamp.digicert.com /d "MyApp Installer" installer.exe
  ```
- [ ] Verify signature:
  ```cmd
  signtool verify /pa installer.exe
  ```
- [ ] Check signature in file properties
- [ ] Verify timestamp present

### Post-Signing Verification

- [ ] Signed executable runs without warnings
- [ ] SmartScreen doesn't block (may require reputation)
- [ ] Antivirus doesn't flag as suspicious
- [ ] Signature details correct in properties

## Package Creation

### Distribution Package

- [ ] Create distribution directory
- [ ] Copy all required files
- [ ] Include documentation
- [ ] Include license files
- [ ] Create RELEASE_INFO.txt
- [ ] Verify package completeness

### Archive Creation

- [ ] Create ZIP archive
- [ ] Verify archive integrity
- [ ] Test extraction
- [ ] Check archive size
- [ ] Name archive appropriately (e.g., `Installer-v1.0.0.zip`)

### Checksums

- [ ] Generate SHA256 checksum:
  ```powershell
  Get-FileHash installer.exe -Algorithm SHA256
  ```
- [ ] Generate MD5 checksum (optional):
  ```powershell
  Get-FileHash installer.exe -Algorithm MD5
  ```
- [ ] Document checksums in release notes
- [ ] Create checksums file:
  ```
  SHA256: <hash>
  MD5: <hash>
  ```

## Release Notes

### Content

- [ ] Version number and release date
- [ ] Summary of changes
- [ ] New features listed
- [ ] Bug fixes listed
- [ ] Known issues documented
- [ ] Breaking changes highlighted (if any)
- [ ] Upgrade instructions (if needed)
- [ ] System requirements updated
- [ ] Credits and acknowledgments

### Format

- [ ] Markdown format for GitHub/GitLab
- [ ] Plain text version for distribution
- [ ] HTML version for website (optional)

## Distribution

### Upload

- [ ] Upload to primary distribution server
- [ ] Upload to backup/mirror servers
- [ ] Upload to CDN (if applicable)
- [ ] Update download links on website
- [ ] Verify download links work

### Version Control

- [ ] Create Git tag for release:
  ```cmd
  git tag -a v1.0.0 -m "Release version 1.0.0"
  git push origin v1.0.0
  ```
- [ ] Create GitHub/GitLab release
- [ ] Attach release artifacts to release
- [ ] Publish release notes

### Package Managers

- [ ] Update Chocolatey package (if applicable)
- [ ] Update Winget manifest (if applicable)
- [ ] Update Scoop manifest (if applicable)
- [ ] Notify package maintainers

## Communication

### Internal

- [ ] Notify development team
- [ ] Notify QA team
- [ ] Notify support team
- [ ] Update internal documentation
- [ ] Update deployment guides

### External

- [ ] Publish release announcement
- [ ] Update website
- [ ] Send email to users (if applicable)
- [ ] Post on social media (if applicable)
- [ ] Update documentation site
- [ ] Notify partners/integrators

## Post-Release

### Monitoring

- [ ] Monitor download statistics
- [ ] Monitor error reports
- [ ] Monitor support tickets
- [ ] Check for crash reports
- [ ] Review user feedback

### Documentation

- [ ] Archive release artifacts
- [ ] Document any issues found
- [ ] Update known issues list
- [ ] Create post-mortem (if needed)

### Planning

- [ ] Schedule next release
- [ ] Plan next features
- [ ] Address feedback
- [ ] Update roadmap

## Emergency Rollback Plan

If critical issues are discovered after release:

### Immediate Actions

- [ ] Remove download links
- [ ] Post warning on website
- [ ] Notify users via email/social media
- [ ] Document the issue

### Investigation

- [ ] Reproduce the issue
- [ ] Identify root cause
- [ ] Assess impact
- [ ] Determine fix approach

### Resolution

- [ ] Develop fix
- [ ] Test fix thoroughly
- [ ] Create hotfix release
- [ ] Follow abbreviated release process
- [ ] Communicate resolution

## Release Approval

### Sign-Off

- [ ] Development lead approval
- [ ] QA lead approval
- [ ] Product manager approval
- [ ] Security review approval (if required)
- [ ] Legal review approval (if required)

### Final Checks

- [ ] All checklist items completed
- [ ] No blocking issues
- [ ] Release notes finalized
- [ ] Distribution ready
- [ ] Support team briefed

### Release Authorization

- [ ] Release authorized by: _______________
- [ ] Date: _______________
- [ ] Version: _______________

## Notes

Use this section to document any deviations from the checklist or special considerations for this release:

```
[Add notes here]
```

## Version History

| Version | Date | Released By | Notes |
|---------|------|-------------|-------|
| 1.0.0 | YYYY-MM-DD | Name | Initial release |

---

**Remember**: Quality over speed. It's better to delay a release than to release with known critical issues.

**Tip**: Create a copy of this checklist for each release and check off items as you complete them. Archive completed checklists for future reference.
