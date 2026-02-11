param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$CaseName = "",
    [switch]$KeepArtifacts,
    [int]$StepTimeoutSec = 300,
    [int]$CleanupWaitSec = 45
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Section {
    param([string]$Message)
    Write-Host ""
    Write-Host "== $Message ==" -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Message)
    Write-Host "  -> $Message" -ForegroundColor DarkCyan
}

function New-RunId {
    return (Get-Date -Format "yyyyMMdd_HHmmss")
}

function Remove-PathRobust {
    param([string]$PathToRemove)

    if ([string]::IsNullOrWhiteSpace($PathToRemove)) {
        return
    }
    if (-not (Test-Path -LiteralPath $PathToRemove)) {
        return
    }

    try {
        Remove-Item -LiteralPath $PathToRemove -Recurse -Force -ErrorAction Stop
        return
    } catch {
    }

    $null = cmd /c "rmdir /s /q \"$PathToRemove\""
}

function Quote-CommandArgument {
    param([string]$Argument)

    if ($null -eq $Argument -or $Argument.Length -eq 0) {
        return '""'
    }

    if ($Argument -notmatch '[\s"]') {
        return $Argument
    }

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $slashCount = 0

    foreach ($ch in $Argument.ToCharArray()) {
        if ($ch -eq '\\') {
            $slashCount++
            continue
        }

        if ($ch -eq '"') {
            [void]$builder.Append('\\' * ($slashCount * 2 + 1))
            [void]$builder.Append('"')
            $slashCount = 0
            continue
        }

        if ($slashCount -gt 0) {
            [void]$builder.Append('\\' * $slashCount)
            $slashCount = 0
        }
        [void]$builder.Append($ch)
    }

    if ($slashCount -gt 0) {
        [void]$builder.Append('\\' * ($slashCount * 2))
    }

    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-External {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [int]$TimeoutSec
    )

    if (-not (Test-Path -LiteralPath $FilePath)) {
        throw "Executable not found: $FilePath"
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.Arguments = ($Arguments | ForEach-Object { Quote-CommandArgument $_ }) -join ' '

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    $null = $proc.Start()
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()

    $finished = $proc.WaitForExit($TimeoutSec * 1000)
    if (-not $finished) {
        try {
            $proc.Kill()
        } catch {
        }
        throw "Process timeout after ${TimeoutSec}s: $FilePath $($Arguments -join ' ')"
    }

    $proc.WaitForExit()
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result

    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        StdOut = $stdout
        StdErr = $stderr
        Command = "$FilePath $($Arguments -join ' ')"
    }
}

function Find-ManifestPath {
    param(
        [string]$TargetPath,
        [string]$AppName
    )

    $candidateA = Join-Path $TargetPath "install.manifest.json"
    if (Test-Path -LiteralPath $candidateA) {
        return $candidateA
    }

    $candidateB = Join-Path (Join-Path $TargetPath $AppName) "install.manifest.json"
    if (Test-Path -LiteralPath $candidateB) {
        return $candidateB
    }

    if (Test-Path -LiteralPath $TargetPath) {
        $all = Get-ChildItem -LiteralPath $TargetPath -Filter "install.manifest.json" -Recurse -File -ErrorAction SilentlyContinue
        if ($all.Count -gt 0) {
            return $all[0].FullName
        }
    }

    return $null
}

function Wait-ForUninstallCleanup {
    param(
        [string]$SentinelPath,
        [string]$ManifestPath,
        [int]$TimeoutSec
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $sentinelExists = Test-Path -LiteralPath $SentinelPath
        $manifestExists = Test-Path -LiteralPath $ManifestPath
        if (-not $sentinelExists -and -not $manifestExists) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Get-MeaningfulRemainingFiles {
    param([string]$InstallRoot)

    if (-not (Test-Path -LiteralPath $InstallRoot)) {
        return @()
    }

    $ignored = @("desktop.ini", "thumbs.db", "uninstall.exe")
    $files = Get-ChildItem -LiteralPath $InstallRoot -Recurse -File -ErrorAction SilentlyContinue
    $result = @()

    foreach ($f in $files) {
        if ($ignored -contains $f.Name.ToLowerInvariant()) {
            continue
        }
        $result += $f.FullName
    }

    return $result
}

function Build-LongTargetPath {
    param([string]$BasePath)

    $path = Join-Path $BasePath "MTI_LongPath_Target"
    $index = 0
    while ($path.Length -lt 170) {
        $segment = "seg_${index}_" + ("x" * 18)
        $path = Join-Path $path $segment
        $index++
        if ($index -gt 20) {
            break
        }
    }
    return $path
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }

$packagerExe = Join-Path $buildRoot "$Config\packager.exe"
Assert-True (Test-Path -LiteralPath $packagerExe) "packager.exe not found: $packagerExe"

$runId = New-RunId
$workspace = Join-Path $buildRoot "tests\stage5_install_uninstall_regression_$runId"
New-Item -ItemType Directory -Force -Path $workspace | Out-Null

$chineseTargetName = "MTI_" + [string][char]0x56DE + [string][char]0x5F52 + [string][char]0x6D4B + [string][char]0x8BD5 + "_" + [string][char]0x4E2D + [string][char]0x6587 + [string][char]0x8DEF + [string][char]0x5F84 + "_" + $runId

$cases = @(
    [pscustomobject]@{ Name = "chinese_path"; Display = "ChinesePath"; Target = (Join-Path $env:TEMP $chineseTargetName) },
    [pscustomobject]@{ Name = "space_path"; Display = "SpacePath"; Target = (Join-Path $env:TEMP "MTI Regression Space Path $runId") },
    [pscustomobject]@{ Name = "long_path"; Display = "LongPath"; Target = (Build-LongTargetPath (Join-Path $env:TEMP "MTI_LongPath_Base_$runId")) }
)

if (-not [string]::IsNullOrWhiteSpace($CaseName)) {
    $cases = @($cases | Where-Object { $_.Name -eq $CaseName })
    Assert-True ($cases.Count -gt 0) "Unknown case name: $CaseName"
}

$results = New-Object System.Collections.Generic.List[object]

try {
    foreach ($case in $cases) {
        Write-Section "Case: $($case.Display)"

        $appName = "MTI_Regress_${runId}_$($case.Name)"
        $inputDir = Join-Path $workspace "input_$($case.Name)"
        $folderDir = Join-Path $inputDir "bin"
        New-Item -ItemType Directory -Force -Path $folderDir | Out-Null

        $sentinelContent = "sentinel::$($case.Name)::$runId"
        $sentinelFileName = "sentinel_$($case.Name).txt"
        $sourceSentinel = Join-Path $folderDir $sentinelFileName
        Set-Content -LiteralPath $sourceSentinel -Value $sentinelContent -Encoding UTF8

        $cfg = [ordered]@{
            Version = "1.0"
            AppName = $appName
            InstallDir = "%LocalAppData%"
            Folder = [ordered]@{
                InstallDir = "bin"
            }
            AutoStartup = $false
            DesktopIcons = $false
            AutoCleanOldInstall = $false
            RequireAdmin = $false
            InstallState = [ordered]@{
                Mode = "Registry"
                RegistryPath = "HKEY_CURRENT_USER\\Software\\$appName"
                RegistryKey = "InstallState"
                FilePath = "%ProgramData%\\$appName\\install.state"
                UseMutex = $true
                MutexName = "Global\\${appName}_Install"
            }
        }

        $cfgPath = Join-Path $inputDir "packager.json"
        $cfgJson = $cfg | ConvertTo-Json -Depth 10
        [System.IO.File]::WriteAllText($cfgPath, $cfgJson, [System.Text.UTF8Encoding]::new($false))

        $installerPath = Join-Path $workspace "setup_$($case.Name).exe"

        Write-Step "Packaging installer"
        $packagerResult = Invoke-External -FilePath $packagerExe `
                                          -Arguments @($inputDir, $installerPath) `
                                          -WorkingDirectory $repoRoot `
                                          -TimeoutSec $StepTimeoutSec

        Assert-True ($packagerResult.ExitCode -eq 0) "Packager failed for $($case.Display). ExitCode=$($packagerResult.ExitCode)`nSTDOUT:`n$($packagerResult.StdOut)`nSTDERR:`n$($packagerResult.StdErr)"
        Assert-True (Test-Path -LiteralPath $installerPath) "Installer not generated: $installerPath"

        Remove-PathRobust -PathToRemove $case.Target

        Write-Step "Running silent install"
        $installResult = Invoke-External -FilePath $installerPath `
                                         -Arguments @("-s", "-f", "-d", $case.Target) `
                                         -WorkingDirectory $workspace `
                                         -TimeoutSec $StepTimeoutSec

        Assert-True ($installResult.ExitCode -eq 0) "Install failed for $($case.Display). ExitCode=$($installResult.ExitCode)`nSTDOUT:`n$($installResult.StdOut)`nSTDERR:`n$($installResult.StdErr)"

        $manifestPath = Find-ManifestPath -TargetPath $case.Target -AppName $appName
        Assert-True (-not [string]::IsNullOrWhiteSpace($manifestPath)) "Manifest not found after install for $($case.Display). Target=$($case.Target)"

        $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $installRoot = [string]$manifest.installDir
        Assert-True (-not [string]::IsNullOrWhiteSpace($installRoot)) "installDir missing in manifest for $($case.Display)"

        $sentinelCandidates = @(
            (Join-Path $installRoot $sentinelFileName),
            (Join-Path (Join-Path $installRoot "bin") $sentinelFileName)
        )
        $installedSentinel = $null
        foreach ($candidate in $sentinelCandidates) {
            if (Test-Path -LiteralPath $candidate) {
                $installedSentinel = $candidate
                break
            }
        }
        if ($null -eq $installedSentinel) {
            $foundSentinels = Get-ChildItem -LiteralPath $installRoot -Filter $sentinelFileName -Recurse -File -ErrorAction SilentlyContinue
            if ($foundSentinels.Count -gt 0) {
                $installedSentinel = $foundSentinels[0].FullName
            }
        }
        Assert-True (-not [string]::IsNullOrWhiteSpace($installedSentinel)) "Installed sentinel missing under: $installRoot"

        $installedContent = Get-Content -LiteralPath $installedSentinel -Raw -Encoding UTF8
        Assert-True ($installedContent.Trim() -eq $sentinelContent) "Installed sentinel content mismatch for $($case.Display)"

        $uninstallExe = [string]$manifest.uninstallPath
        if ([string]::IsNullOrWhiteSpace($uninstallExe)) {
            $uninstallExe = Join-Path $installRoot "uninstall.exe"
        }
        Assert-True (Test-Path -LiteralPath $uninstallExe) "uninstall.exe not found: $uninstallExe"

        Write-Step "Running silent uninstall"
        $uninstallResult = Invoke-External -FilePath $uninstallExe `
                                           -Arguments @("--uninstall", "-s") `
                                           -WorkingDirectory (Split-Path -Parent $uninstallExe) `
                                           -TimeoutSec $StepTimeoutSec

        Assert-True ($uninstallResult.ExitCode -eq 0) "Uninstall failed for $($case.Display). ExitCode=$($uninstallResult.ExitCode)`nSTDOUT:`n$($uninstallResult.StdOut)`nSTDERR:`n$($uninstallResult.StdErr)"

        Write-Step "Validating cleanup"
        $cleanupOk = Wait-ForUninstallCleanup -SentinelPath $installedSentinel -ManifestPath $manifestPath -TimeoutSec $CleanupWaitSec
        Assert-True $cleanupOk "Cleanup timeout for $($case.Display). Sentinel or manifest still exists."

        $remainingFiles = @(Get-MeaningfulRemainingFiles -InstallRoot $installRoot)
        Assert-True ($remainingFiles.Count -eq 0) "Unexpected files remain after uninstall for $($case.Display):`n$($remainingFiles -join "`n")"

        $results.Add([pscustomobject]@{
            Case = $case.Display
            AppName = $appName
            Target = $case.Target
            InstallRoot = $installRoot
            Status = "PASS"
        })
    }

    Write-Section "Summary"
    $results | Format-Table -AutoSize
    Write-Host "All stage 5 path regression cases passed." -ForegroundColor Green
} finally {
    if (-not $KeepArtifacts) {
        Remove-PathRobust -PathToRemove $workspace
    } else {
        Write-Host "Artifacts kept at: $workspace" -ForegroundColor Yellow
    }
}



