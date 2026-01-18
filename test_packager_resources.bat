@echo off
echo ========================================
echo Testing Packager-Generated Installer
echo ========================================
echo.

echo Step 1: Rebuilding installer with debug logging...
cmake --build build --config Release --target installer
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to build installer
    exit /b 1
)
echo.

echo Step 2: Running packager to generate installer...
build\Release\packager.exe test_input build\Release\output\MyApp_Setup.exe
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Packager failed
    exit /b 1
)
echo.

echo Step 3: Checking output directory contents...
echo Contents of build\Release\output\:
dir /b build\Release\output\
echo.

echo Step 4: Checking resources directory...
if exist "build\Release\output\resources" (
    echo Resources directory EXISTS
    echo Contents:
    dir /b /s build\Release\output\resources\
) else (
    echo ERROR: Resources directory NOT FOUND
    exit /b 1
)
echo.

echo Step 5: Checking DLL files...
if exist "build\Release\output\DuiLib.dll" (
    echo DuiLib.dll EXISTS
) else (
    echo ERROR: DuiLib.dll NOT FOUND
)

if exist "build\Release\output\liblzma.dll" (
    echo liblzma.dll EXISTS
) else (
    echo ERROR: liblzma.dll NOT FOUND
)
echo.

echo Step 6: Running packager-generated installer...
echo Press any key to run the installer (check console output for debug info)
pause
build\Release\output\MyApp_Setup.exe

echo.
echo Test complete!
