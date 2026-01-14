@echo off
echo Testing installation paths...
echo.

REM Create a temporary test directory
set TEST_DIR=%TEMP%\test_install_%RANDOM%
mkdir "%TEST_DIR%"

echo Installing to: %TEST_DIR%
echo.

REM Run the installer with the test directory
echo. | .\build\Release\test_installer.exe

echo.
echo Installation complete. Checking installed files...
echo.

REM Check if files were installed to the correct locations
if exist "%TEST_DIR%\TestApp" (
    echo [OK] TestApp folder found in installation directory
) else (
    echo [FAIL] TestApp folder NOT found in installation directory
)

if exist "%APPDATA%\Roaming\TestApp" (
    echo [OK] TestApp folder found in AppData\Roaming
) else (
    echo [FAIL] TestApp folder NOT found in AppData\Roaming
)

echo.
echo Cleanup...
rmdir /s /q "%TEST_DIR%" 2>nul
rmdir /s /q "%APPDATA%\Roaming\TestApp" 2>nul

echo.
echo Test complete.
pause
