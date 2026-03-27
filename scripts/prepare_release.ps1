# Release Preparation Script
# This script builds the installer in Release mode and prepares a distribution package.
# Generated installers embed UI resources; distribution does not require an external
# resources/ directory.

param(
    [string]$Version = "1.0.0",
    [switch]$Clean,
    [string]$BuildDir = "build-release"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Installer Release Preparation v$Version" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[1/6] Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
    Write-Host "  Build directory cleaned" -ForegroundColor Green
} else {
    Write-Host "[1/6] Using existing build directory" -ForegroundColor Yellow
}
Write-Host ""

Write-Host "[2/6] Creating build directory..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Write-Host "  Build directory ready: $BuildDir" -ForegroundColor Green
Write-Host ""

Write-Host "[3/6] Configuring with CMake..." -ForegroundColor Yellow
Push-Location $BuildDir
try {
    $cmakeArgs = @(
        "..",
        "-G", "Visual Studio 16 2019",
        "-A", "x64",
        "-DBUILD_GUI=ON",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DSTATIC_LINK_RUNTIME=ON"
    )

    & cmake $cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }
    Write-Host "  CMake configuration successful" -ForegroundColor Green
} finally {
    Pop-Location
}
Write-Host ""

Write-Host "[4/6] Building Release version..." -ForegroundColor Yellow
Push-Location $BuildDir
try {
    & cmake --build . --config Release --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
    Write-Host "  Build successful" -ForegroundColor Green
} finally {
    Pop-Location
}
Write-Host ""

Write-Host "[5/6] Preparing distribution package..." -ForegroundColor Yellow

$DistDir = "dist-v$Version"
$Timestamp = Get-Date -Format "yyyyMMdd-HHmmss"

if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$InstallerExe = Join-Path $BuildDir "Release\installer.exe"
if (Test-Path $InstallerExe) {
    Copy-Item $InstallerExe $DistDir
    Write-Host "  Copied installer.exe" -ForegroundColor Green
} else {
    throw "Installer executable not found: $InstallerExe"
}

$PackagerExe = Join-Path $BuildDir "Release\packager.exe"
if (Test-Path $PackagerExe) {
    Copy-Item $PackagerExe $DistDir
    Write-Host "  Copied packager.exe" -ForegroundColor Green
}

$DocsToInclude = @(
    "README.md",
    "LICENSE",
    "docs/USER_GUIDE.md",
    "docs/REQUIREMENTS.md",
    "docs/DETAILED_DESIGN.md"
)

$DocsDir = Join-Path $DistDir "docs"
New-Item -ItemType Directory -Force -Path $DocsDir | Out-Null

foreach ($Doc in $DocsToInclude) {
    if (Test-Path $Doc) {
        $DestPath = if ($Doc -like "docs/*") {
            Join-Path $DocsDir (Split-Path $Doc -Leaf)
        } else {
            Join-Path $DistDir (Split-Path $Doc -Leaf)
        }
        Copy-Item $Doc $DestPath
        Write-Host "  Copied $(Split-Path $Doc -Leaf)" -ForegroundColor Green
    } else {
        Write-Host "  Document not found: $Doc" -ForegroundColor Yellow
    }
}

$DllsDir = Join-Path $BuildDir "Release"
$RequiredDlls = @("libzstd.dll")

foreach ($Dll in $RequiredDlls) {
    $DllPath = Join-Path $DllsDir $Dll
    if (Test-Path $DllPath) {
        Copy-Item $DllPath $DistDir
        Write-Host "  Copied $Dll" -ForegroundColor Green
    }
}

Write-Host "  Distribution package prepared: $DistDir" -ForegroundColor Green
Write-Host ""

Write-Host "[6/6] Creating release archive..." -ForegroundColor Yellow

$ArchiveName = "Installer-v$Version-$Timestamp.zip"
Compress-Archive -Path "$DistDir\*" -DestinationPath $ArchiveName -Force

$ArchiveSize = (Get-Item $ArchiveName).Length / 1MB
Write-Host "  Archive created: $ArchiveName ($([math]::Round($ArchiveSize, 2)) MB)" -ForegroundColor Green
Write-Host ""

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Release Preparation Complete!" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Distribution package: $DistDir" -ForegroundColor White
Write-Host "Release archive: $ArchiveName" -ForegroundColor White
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Test the installer in $DistDir" -ForegroundColor White
Write-Host "  2. Sign the executable (if applicable)" -ForegroundColor White
Write-Host "  3. Create release notes" -ForegroundColor White
Write-Host "  4. Upload $ArchiveName to distribution server" -ForegroundColor White
Write-Host ""

$ReleaseInfo = @"
Release Information
===================

Version: $Version
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Build Type: Release
Platform: Windows x64
Static Runtime: Yes

Files Included:
- installer.exe (Main installer with GUI)
- packager.exe (Package creation tool)
- docs/ (User and reference documentation)

Notes:
- Installer UI resources are embedded into generated installers.
- External resources/ directories are not required for distribution.

Build Configuration:
- CMake Generator: Visual Studio 16 2019
- Architecture: x64
- GUI Support: Enabled (DuiLib)
- Static Linking: Enabled

System Requirements:
- Windows 7 or later
- 100 MB free disk space (minimum)
- Administrator privileges (recommended)

Installation:
1. Extract all files to a directory
2. Run installer.exe
3. Follow the on-screen instructions

For silent installation:
  installer.exe -s

For more information, see docs/USER_GUIDE.md

"@

$ReleaseInfo | Out-File -FilePath (Join-Path $DistDir "RELEASE_INFO.txt") -Encoding UTF8
Write-Host "Release info saved to: $DistDir\RELEASE_INFO.txt" -ForegroundColor Green
Write-Host ""
