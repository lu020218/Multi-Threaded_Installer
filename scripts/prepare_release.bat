@echo off
REM Release Preparation Script (Batch version)
REM Generated installers embed UI resources; distribution does not require an
REM external resources/ directory.

setlocal enabledelayedexpansion

set VERSION=1.0.0
set BUILD_DIR=build-release
set DIST_DIR=dist-v%VERSION%

echo ========================================
echo   Installer Release Preparation v%VERSION%
echo ========================================
echo.

if exist %BUILD_DIR% (
    echo [1/6] Cleaning build directory...
    rmdir /s /q %BUILD_DIR%
    echo   [OK] Build directory cleaned
) else (
    echo [1/6] Build directory does not exist
)
echo.

echo [2/6] Creating build directory...
mkdir %BUILD_DIR%
echo   [OK] Build directory ready: %BUILD_DIR%
echo.

echo [3/6] Configuring with CMake...
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

echo [4/6] Building Release version...
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

echo [5/6] Preparing distribution package...

if exist %DIST_DIR% (
    rmdir /s /q %DIST_DIR%
)
mkdir %DIST_DIR%
mkdir %DIST_DIR%\docs

if exist %BUILD_DIR%\Release\installer.exe (
    copy %BUILD_DIR%\Release\installer.exe %DIST_DIR%\ > nul
    echo   [OK] Copied installer.exe
) else (
    echo   [ERROR] Installer executable not found
    exit /b 1
)

if exist %BUILD_DIR%\Release\packager.exe (
    copy %BUILD_DIR%\Release\packager.exe %DIST_DIR%\ > nul
    echo   [OK] Copied packager.exe
)

if exist README.md copy README.md %DIST_DIR%\ > nul
if exist LICENSE copy LICENSE %DIST_DIR%\ > nul
if exist docs\USER_GUIDE.md copy docs\USER_GUIDE.md %DIST_DIR%\docs\ > nul
if exist docs\REQUIREMENTS.md copy docs\REQUIREMENTS.md %DIST_DIR%\docs\ > nul
if exist docs\DETAILED_DESIGN.md copy docs\DETAILED_DESIGN.md %DIST_DIR%\docs\ > nul
echo   [OK] Copied documentation

if exist %BUILD_DIR%\Release\libzstd.dll (
    copy %BUILD_DIR%\Release\libzstd.dll %DIST_DIR%\ > nul
    echo   [OK] Copied libzstd.dll
)

echo   [OK] Distribution package prepared: %DIST_DIR%
echo.

echo [6/6] Creating release archive...
set TIMESTAMP=%date:~-4%%date:~-10,2%%date:~-7,2%-%time:~0,2%%time:~3,2%%time:~6,2%
set TIMESTAMP=%TIMESTAMP: =0%
set ARCHIVE_NAME=Installer-v%VERSION%-%TIMESTAMP%.zip

powershell -Command "Compress-Archive -Path '%DIST_DIR%\*' -DestinationPath '%ARCHIVE_NAME%' -Force"
if errorlevel 1 (
    echo   [WARNING] Could not create ZIP archive
    echo   Please manually create archive from %DIST_DIR%
) else (
    echo   [OK] Archive created: %ARCHIVE_NAME%
)
echo.

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
echo - docs/ ^(User and reference documentation^)
echo.
echo Notes:
echo - Installer UI resources are embedded into generated installers.
echo - External resources/ directories are not required for distribution.
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
