# PowerShell script to package installer with all runtime dependencies
# Usage: .\package_with_dependencies.ps1 -InputDir "input" -OutputFile "output\MyApp_Setup.exe"

param(
    [Parameter(Mandatory=$true)]
    [string]$InputDir,
    
    [Parameter(Mandatory=$true)]
    [string]$OutputFile
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Packaging Installer with Dependencies" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

Write-Host "Input: $InputDir" -ForegroundColor Green
Write-Host "Output: $OutputFile" -ForegroundColor Green
Write-Host ""

# Step 1: Run packager
Write-Host "[1/2] Running packager..." -ForegroundColor Yellow
$packagerExe = "build\Release\packager.exe"

if (-not (Test-Path $packagerExe)) {
    Write-Host "ERROR: Packager not found at $packagerExe" -ForegroundColor Red
    Write-Host "Please build the packager first: cmake --build build --config Release --target packager" -ForegroundColor Red
    exit 1
}

& $packagerExe $InputDir $OutputFile
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Packager failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit 1
}

Write-Host "  OK: Installer created" -ForegroundColor Gray
Write-Host ""

# Step 2: Copy dependencies
Write-Host "[2/2] Copying runtime dependencies..." -ForegroundColor Yellow

$outputDir = Split-Path -Parent $OutputFile
if ([string]::IsNullOrEmpty($outputDir)) {
    $outputDir = "."
}

Write-Host "Output directory: $outputDir" -ForegroundColor Gray
Write-Host ""

$allSuccess = $true

# Copy DuiLib.dll
$duilib = "build\Release\DuiLib.dll"
if (Test-Path $duilib) {
    try {
        Copy-Item $duilib -Destination $outputDir -Force
        Write-Host "  OK: Copied DuiLib.dll" -ForegroundColor Gray
    }
    catch {
        Write-Host "  ERROR: Failed to copy DuiLib.dll - $_" -ForegroundColor Red
        $allSuccess = $false
    }
}
else {
    Write-Host "  WARNING: DuiLib.dll not found at $duilib" -ForegroundColor Yellow
    $allSuccess = $false
}

# liblzma is linked statically; no DLL to copy

# Copy resources directory
$resources = "build\Release\resources"
if (Test-Path $resources) {
    try {
        $destResources = Join-Path $outputDir "resources"
        if (Test-Path $destResources) {
            Remove-Item $destResources -Recurse -Force
        }
        Copy-Item $resources -Destination $destResources -Recurse -Force
        Write-Host "  OK: Copied resources directory" -ForegroundColor Gray
    }
    catch {
        Write-Host "  ERROR: Failed to copy resources directory - $_" -ForegroundColor Red
        $allSuccess = $false
    }
}
else {
    Write-Host "  WARNING: resources directory not found at $resources" -ForegroundColor Yellow
    $allSuccess = $false
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Packaging Complete!" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if ($allSuccess) {
    Write-Host "All dependencies copied successfully!" -ForegroundColor Green
}
else {
    Write-Host "Some dependencies were not copied. Check warnings above." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Output files in $outputDir :" -ForegroundColor Cyan
Get-ChildItem $outputDir | Select-Object Name, Length | Format-Table -AutoSize

Write-Host ""
Write-Host "To run the installer:" -ForegroundColor Green
Write-Host "  cd $outputDir" -ForegroundColor Gray
Write-Host "  .\$(Split-Path -Leaf $OutputFile)" -ForegroundColor Gray
Write-Host ""
