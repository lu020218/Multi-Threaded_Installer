@echo off
REM Automated script to build a single-file installer
REM This script builds the installer and embeds all resources

setlocal enabledelayedexpansion

echo ========================================
echo  Single-File Installer Build Script
echo ========================================
echo.

REM Check if we're in the project root
if not exist "CMakeLists.txt" (
    echo ERROR: Please run this script from the project root directory
    exit /b 1
)

REM Step 1: Build the installer
echo [1/3] Building installer...
cmake --build build --config Release --target installer
if errorlevel 1 (
    echo ERROR: Failed to build installer
    exit /b 1
)
echo   OK: Installer built successfully
echo.

REM Step 2: Check if resources exist
echo [2/3] Checking resources...
if not exist "build\Release\DuiLib.dll" (
    echo WARNING: DuiLib.dll not found (assuming static DuiLib)
)
if not exist "build\Release\resources\skins\main.xml" (
    echo ERROR: Resources not found
    exit /b 1
)
echo   OK: All resources found
echo.

REM Step 3: Embed resources
echo [3/3] Embedding resources into installer...
powershell -ExecutionPolicy Bypass -File scripts\embed_resources.ps1 -InstallerPath build\Release\installer.exe
if errorlevel 1 (
    echo ERROR: Failed to embed resources
    exit /b 1
)
echo.

REM Show result
echo ========================================
echo  BUILD SUCCESSFUL!
echo ========================================
echo.
echo Single-file installer created:
echo   build\Release\installer.exe
echo.

REM Show file size
for %%F in (build\Release\installer.exe) do (
    set size=%%~zF
    set /a sizeMB=!size! / 1048576
    echo File size: !sizeMB! MB
)

echo.
echo You can now distribute just this one file!
echo.
echo To test:
echo   1. Copy installer.exe to a clean directory
echo   2. Run it (no other files needed)
echo.

endlocal
