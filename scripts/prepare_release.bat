@echo off
REM Release Preparation Script (Batch version)
REM This script builds the installer in Release mode and prepares a distribution package

setlocal enabledelayedexpansion

set VERSION=1.0.0
set BUILD_DIR=build-release
set DIST_DIR=dist-v%VERSION%

echo ========================================
echo   Installer Release Preparation v%VERSION%
echo ========================================
echo.

REM Step 1: Clean build directory (optional)
if exist %BUILD_DIR% (
    echo [1/7] Cleaning build directory...
    rmdir /s /q %BUILD_DIR%
    echo   [OK] Build directory cleaned
) else (
    echo [1/7] Build directory does not exist
)
echo.

REM Step 2: Create build directory
echo [2/7] Creating build directory...
mkdir %BUILD_DIR%
echo   [OK] Build directory ready: %BUILD_DIR%
echo.

REM Step 3: Configure with CMake
echo [3/7] Configuring with CMake...
cd %BUILD_DIR%
cmake .. -G "Visual Studio 16 2019" -A x64 -DBUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release -DSTATIC_LINK_RUNTIME=ON
if errorlevel 1 (
    echo   [ERROR] CMake configuration failed
    cd ..
    exit /b 1
)
echo   [OK] CMake configuration successful
cd ..
echo.

REM Step 4: Build Release version
echo [4/7] Building Release version...
cd %BUILD_DIR%
cmake --build . --config Release --parallel
if errorlevel 1 (
    echo   [ERROR] Build failed
    cd ..
    exit /b 1
)
echo   [OK] Build successful
cd ..
echo.

REM Step 5: Run tests
echo [5/7] Running tests...
cd %BUILD_DIR%
ctest -C Release --output-on-failure
if errorlevel 1 (
    echo   [WARNING] Some tests failed, but continuing...
) else (
    echo   [OK] All tests passed
)
cd ..
echo.

REM Step 6: Prepare distribution package
echo [6/7] Preparing distribution package...

REM Clean and create distribution directory
if exist %DIST_DIR% (
    rmdir /s /q %DIST_DIR%
)
mkdir %DIST_DIR%
mkdir %DIST_DIR%\docs

REM Copy installer executable
if exist %BUILD_DIR%\Release\installer.exe (
    copy %BUILD_DIR%\Release\installer.exe %DIST_DIR%\
    echo   [OK] Copied installer.exe
) else (
    echo   [ERROR] Installer executable not found
    exit /b 1
)

REM Copy packager executable
if exist %BUILD_DIR%\Release\packager.exe (
    copy %BUILD_DIR%\Release\packager.exe %DIST_DIR%\
    echo   [OK] Copied packager.exe
)

REM Copy resources directory
if exist resources (
    xcopy /E /I /Y resources %DIST_DIR%\resources > nul
    echo   [OK] Copied resources directory
) else (
    echo   [WARNING] Resources directory not found
)

REM Copy documentation
if exist README.md copy README.md %DIST_DIR%\ > nul
if exist LICENSE copy LICENSE %DIST_DIR%\ > nul
if exist docs\USER_GUIDE.md copy docs\USER_GUIDE.md %DIST_DIR%\docs\ > nul
if exist docs\COMMAND_LINE_REFERENCE.md copy docs\COMMAND_LINE_REFERENCE.md %DIST_DIR%\docs\ > nul
if exist docs\TROUBLESHOOTING.md copy docs\TROUBLESHOOTING.md %DIST_DIR%\docs\ > nul
echo   [OK] Copied documentation

REM Copy any required DLLs
if exist %BUILD_DIR%\Release\liblzma.dll (
    copy %BUILD_DIR%\Release\liblzma.dll %DIST_DIR%\ > nul
    echo   [OK] Copied liblzma.dll
)
if exist %BUILD_DIR%\Release\libzstd.dll (
    copy %BUILD_DIR%\Release\libzstd.dll %DIST_DIR%\ > nul
    echo   [OK] Copied libzstd.dll
)

echo   [OK] Distribution package prepared: %DIST_DIR%
echo.

REM Step 7: Create release archive
echo [7/7] Creating release archive...
set TIMESTAMP=%date:~-4%%date:~-10,2%%date:~-7,2%-%time:~0,2%%time:~3,2%%time:~6,2%
set TIMESTAMP=%TIMESTAMP: =0%
set ARCHIVE_NAME=Installer-v%VERSION%-%TIMESTAMP%.zip

REM Use PowerShell to create ZIP (available on Windows 7+)
powershell -Command "Compress-Archive -Path '%DIST_DIR%\*' -DestinationPath '%ARCHIVE_NAME%' -Force"
if errorlevel 1 (
    echo   [WARNING] Could not create ZIP archive
    echo   Please manually create archive from %DIST_DIR%
) else (
    echo   [OK] Archive created: %ARCHIVE_NAME%
)
echo.

REM Create release info file
(
echo Release Information
echo ===================
echo.
echo Version: %VERSION%
echo Build Date: %date% %time%
echo Build Type: Release
echo Platform: Windows x64
echo Static Runtime: Yes
echo.
echo Files Included:
echo - installer.exe ^(Main installer with GUI^)
echo - packager.exe ^(Package creation tool^)
echo - resources/ ^(UI resources: XML layouts and images^)
echo - docs/ ^(User and reference documentation^)
echo.
echo Build Configuration:
echo - CMake Generator: Visual Studio 16 2019
echo - Architecture: x64
echo - GUI Support: Enabled ^(DuiLib^)
echo - Static Linking: Enabled
echo.
echo System Requirements:
echo - Windows 7 or later
echo - 100 MB free disk space ^(minimum^)
echo - Administrator privileges ^(recommended^)
echo.
echo Installation:
echo 1. Extract all files to a directory
echo 2. Run installer.exe
echo 3. Follow the on-screen instructions
echo.
echo For silent installation:
echo   installer.exe -s
echo.
echo For more information, see docs\USER_GUIDE.md
) > %DIST_DIR%\RELEASE_INFO.txt

echo ========================================
echo   Release Preparation Complete!
echo ========================================
echo.
echo Distribution package: %DIST_DIR%
echo Release archive: %ARCHIVE_NAME%
echo.
echo Next steps:
echo   1. Test the installer in %DIST_DIR%
echo   2. Sign the executable ^(if applicable^)
echo   3. Create release notes
echo   4. Upload %ARCHIVE_NAME% to distribution server
echo.

endlocal
