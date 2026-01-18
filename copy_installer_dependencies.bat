@echo off
REM Script to manually copy installer runtime dependencies

set SOURCE_DIR=build\Release
set TARGET_DIR=%1

if "%TARGET_DIR%"=="" (
    echo Usage: copy_installer_dependencies.bat target_directory
    echo Example: copy_installer_dependencies.bat output
    exit /b 1
)

echo Copying runtime dependencies to %TARGET_DIR%...

REM Create target directory if it doesn't exist
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"

REM Copy DLL files
echo Copying DuiLib.dll...
copy /Y "%SOURCE_DIR%\DuiLib.dll" "%TARGET_DIR%\" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy DuiLib.dll
) else (
    echo   OK: DuiLib.dll
)

echo Copying liblzma.dll...
copy /Y "%SOURCE_DIR%\liblzma.dll" "%TARGET_DIR%\" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy liblzma.dll
) else (
    echo   OK: liblzma.dll
)

REM Copy resources directory
echo Copying resources directory...
xcopy /E /I /Y "%SOURCE_DIR%\resources" "%TARGET_DIR%\resources" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy resources directory
) else (
    echo   OK: resources directory
)

echo.
echo Done! Runtime dependencies copied to %TARGET_DIR%
echo.
echo Files in %TARGET_DIR%:
dir /B "%TARGET_DIR%"

exit /b 0
