# PowerShell script to embed DLL and resource files into the installer executable
# This creates a truly single-file installer

param(
    [Parameter(Mandatory=$true)]
    [string]$InstallerPath,
    
    [Parameter(Mandatory=$false)]
    [string]$ResourceDir = "build\Release\resources",
    
    [Parameter(Mandatory=$false)]
    [string]$DuiLibDll = "build\Release\DuiLib.dll"
)

Write-Host "=== Embedding Resources into Installer ===" -ForegroundColor Cyan
Write-Host ""

# Check if installer exists
if (-not (Test-Path $InstallerPath)) {
    Write-Host "ERROR: Installer not found: $InstallerPath" -ForegroundColor Red
    exit 1
}

Write-Host "Installer: $InstallerPath" -ForegroundColor Green

# Function to append binary data to file
function Append-BinaryData {
    param(
        [string]$TargetFile,
        [byte[]]$Data,
        [string]$ResourceName
    )
    
    try {
        $stream = [System.IO.File]::Open($TargetFile, [System.IO.FileMode]::Append)
        
        # Write resource header
        # Format: [NAME_LENGTH(4)][NAME][DATA_LENGTH(8)][DATA]
        $nameBytes = [System.Text.Encoding]::UTF8.GetBytes($ResourceName)
        $nameLength = [BitConverter]::GetBytes([int]$nameBytes.Length)
        $dataLength = [BitConverter]::GetBytes([long]$Data.Length)
        
        $stream.Write($nameLength, 0, 4)
        $stream.Write($nameBytes, 0, $nameBytes.Length)
        $stream.Write($dataLength, 0, 8)
        $stream.Write($Data, 0, $Data.Length)
        
        $stream.Close()
        
        Write-Host "  Embedded: $ResourceName ($($Data.Length) bytes)" -ForegroundColor Gray
        return $true
    }
    catch {
        Write-Host "  ERROR embedding $ResourceName : $_" -ForegroundColor Red
        if ($stream) { $stream.Close() }
        return $false
    }
}

# Embed DuiLib.dll
if (Test-Path $DuiLibDll) {
    Write-Host "Embedding DuiLib.dll..." -ForegroundColor Yellow
    $data = [System.IO.File]::ReadAllBytes($DuiLibDll)
    Append-BinaryData -TargetFile $InstallerPath -Data $data -ResourceName "DUILIB_DLL"
} else {
    Write-Host "WARNING: DuiLib.dll not found at $DuiLibDll" -ForegroundColor Yellow
}

# liblzma.dll is linked statically; no embedding required.

# Embed XML files
if (Test-Path "$ResourceDir\skins") {
    Write-Host "Embedding XML layout files..." -ForegroundColor Yellow
    $xmlFiles = Get-ChildItem "$ResourceDir\skins\*.xml"
    foreach ($file in $xmlFiles) {
        $data = [System.IO.File]::ReadAllBytes($file.FullName)
        $resourceName = "XML_" + $file.Name.ToUpper().Replace(".", "_")
        Append-BinaryData -TargetFile $InstallerPath -Data $data -ResourceName $resourceName
    }
} else {
    Write-Host "WARNING: Resources directory not found at $ResourceDir" -ForegroundColor Yellow
}

# Embed image files
$imageList = @()
if (Test-Path "$ResourceDir\images") {
    Write-Host "Embedding image files..." -ForegroundColor Yellow
    $imageFiles = Get-ChildItem "$ResourceDir\images"
    foreach ($file in $imageFiles) {
        if ($file.PSIsContainer) { continue }
        if ($file.Name.StartsWith(".")) { continue }
        $data = [System.IO.File]::ReadAllBytes($file.FullName)
        $resourceName = "IMG_" + $file.Name.ToUpper().Replace(".", "_")
        if (Append-BinaryData -TargetFile $InstallerPath -Data $data -ResourceName $resourceName) {
            $imageList += $file.Name
        }
    }
} else {
    Write-Host "WARNING: Images directory not found at $ResourceDir" -ForegroundColor Yellow
}

if ($imageList.Count -gt 0) {
    $listText = ($imageList -join "`n") + "`n"
    $listData = [System.Text.Encoding]::UTF8.GetBytes($listText)
    Append-BinaryData -TargetFile $InstallerPath -Data $listData -ResourceName "IMAGES_LIST"
}

# Embed license.txt
if (Test-Path "$ResourceDir\license.txt") {
    Write-Host "Embedding license.txt..." -ForegroundColor Yellow
    $data = [System.IO.File]::ReadAllBytes("$ResourceDir\license.txt")
    Append-BinaryData -TargetFile $InstallerPath -Data $data -ResourceName "LICENSE_TXT"
}

# Write resource index at the end
Write-Host "Writing resource index..." -ForegroundColor Yellow
try {
    $stream = [System.IO.File]::Open($InstallerPath, [System.IO.FileMode]::Append)
    
    # Write magic number to mark end of embedded resources
    $magic = [BitConverter]::GetBytes([uint32]0x52534D45) # "EMSR" in hex
    $stream.Write($magic, 0, 4)
    
    $stream.Close()
    Write-Host "  Resource index written" -ForegroundColor Gray
}
catch {
    Write-Host "  ERROR writing resource index: $_" -ForegroundColor Red
    if ($stream) { $stream.Close() }
}

Write-Host ""
Write-Host "=== Resource Embedding Complete ===" -ForegroundColor Cyan

# Show final file size
$fileInfo = Get-Item $InstallerPath
$sizeMB = [math]::Round($fileInfo.Length / 1MB, 2)
Write-Host "Final installer size: $sizeMB MB" -ForegroundColor Green
Write-Host ""
Write-Host "The installer is now a single-file executable!" -ForegroundColor Green
Write-Host "You can distribute just: $InstallerPath" -ForegroundColor Green
