@echo off
REM Quick script to copy dependencies to installer output directory
REM Usage: copy_deps.bat output_directory

set OUTPUT_DIR=%1
if "%OUTPUT_DIR%"=="" (
    echo Usage: copy_deps.bat output_directory
    echo Example: copy_deps.bat output
    exit /b 1
)

echo Copying dependencies to %OUTPUT_DIR%...
echo.

copy /Y build\Release\DuiLib.dll "%OUTPUT_DIR%\" 2>nul
if errorlevel 1 (
    echo ERROR: Failed to copy DuiLib.dll
) else (
    echo OK: DuiLib.dll
)

copy /Y build\Release\liblzma.dll "%OUTPUT_DIR%\" 2>nul
if errorlevel 1 (
    echo ERROR: Failed to copy liblzma.dll
) else (
    echo OK: liblzma.dll
)

xcopy /E /I /Y build\Release\resources "%OUTPUT_DIR%\resources" >nul 2>nul
if errorlevel 1 (
    echo ERROR: Failed to copy resources
) else (
    echo OK: resources directory
)

echo.
echo Done! Files in %OUTPUT_DIR%:
dir /B "%OUTPUT_DIR%"
