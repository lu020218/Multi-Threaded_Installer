@echo off
REM Script to package installer with all runtime dependencies
REM Usage: package_with_dependencies.bat input_dir output_installer.exe

setlocal enabledelayedexpansion

if "%~1"=="" (
    echo Usage: package_with_dependencies.bat input_directory output_installer.exe
    echo.
    echo Example:
    echo   package_with_dependencies.bat build\Release\input output\MyApp_Setup.exe
    exit /b 1
)

if "%~2"=="" (
    echo Usage: package_with_dependencies.bat input_directory output_installer.exe
    exit /b 1
)

set INPUT_DIR=%~1
set OUTPUT_FILE=%~2

echo ========================================
echo  Packaging Installer with Dependencies
echo ========================================
echo.
echo Input: %INPUT_DIR%
echo Output: %OUTPUT_FILE%
echo.

REM Step 1: Run packager
echo [1/2] Running packager...
build\Release\packager.exe "%INPUT_DIR%" "%OUTPUT_FILE%"
if errorlevel 1 (
    echo ERROR: Packager failed
    exit /b 1
)
echo   OK: Installer created
echo.

REM Step 2: Copy dependencies to output directory
echo [2/2] Copying runtime dependencies...

REM Get output directory
for %%F in ("%OUTPUT_FILE%") do set OUTPUT_DIR=%%~dpF

REM Remove trailing backslash
if "%OUTPUT_DIR:~-1%"=="\" set OUTPUT_DIR=%OUTPUT_DIR:~0,-1%

echo Output directory: %OUTPUT_DIR%
echo.

REM Copy DuiLib.dll
if exist "build\Release\DuiLib.dll" (
    copy /Y "build\Release\DuiLib.dll" "%OUTPUT_DIR%\" >nul
    if errorlevel 1 (
        echo   ERROR: Failed to copy DuiLib.dll
    ) else (
        echo   OK: Copied DuiLib.dll
    )
) else (
    echo   WARNING: DuiLib.dll not found
)

REM liblzma is linked statically; no DLL to copy

REM Copy resources directory
if exist "build\Release\resources" (
    if exist "%OUTPUT_DIR%\resources" (
        rmdir /S /Q "%OUTPUT_DIR%\resources" 2>nul
    )
    xcopy /E /I /Y "build\Release\resources" "%OUTPUT_DIR%\resources" >nul
    if errorlevel 1 (
        echo   ERROR: Failed to copy resources directory
    ) else (
        echo   OK: Copied resources directory
    )
) else (
    echo   WARNING: resources directory not found
)

echo.
echo ========================================
echo  Packaging Complete!
echo ========================================
echo.
echo Output files:
dir /B "%OUTPUT_DIR%"
echo.
echo To run the installer:
echo   cd "%OUTPUT_DIR%"
echo   %~nxF
echo.

endlocal
