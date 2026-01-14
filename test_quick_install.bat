@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Quick Installation Path Test
echo ========================================
echo.

REM Set test directory
set "TEST_DIR=%TEMP%\kiro_test_%RANDOM%"
echo Test installation directory: %TEST_DIR%
echo.

REM Run installer with test directory (provide input via echo)
echo %TEST_DIR%| .\build\Release\test_installer.exe

echo.
echo ========================================
echo Checking Installation Results
echo ========================================
echo.

REM Check TestApp folder (should be in TEST_DIR)
if exist "%TEST_DIR%\TestApp" (
    echo [PASS] TestApp folder found in: %TEST_DIR%\TestApp
    dir "%TEST_DIR%\TestApp" /b | find /c /v "" > nul
    if !errorlevel! equ 0 (
        echo        Files found in TestApp folder
    )
) else (
    echo [FAIL] TestApp folder NOT found in: %TEST_DIR%\TestApp
)

echo.

REM Check TestPlugins folder (should be in AppData\Roaming\TestApp)
if exist "%APPDATA%\TestApp\TestPlugins" (
    echo [PASS] TestPlugins folder found in: %APPDATA%\TestApp\TestPlugins
    dir "%APPDATA%\TestApp\TestPlugins" /b | find /c /v "" > nul
    if !errorlevel! equ 0 (
        echo        Files found in TestPlugins folder
    )
) else (
    echo [FAIL] TestPlugins folder NOT found in: %APPDATA%\TestApp\TestPlugins
)

echo.
echo ========================================
echo Cleanup
echo ========================================

REM Cleanup
if exist "%TEST_DIR%" (
    echo Removing test directory: %TEST_DIR%
    rmdir /s /q "%TEST_DIR%" 2>nul
)

if exist "%APPDATA%\TestApp" (
    echo Removing AppData directory: %APPDATA%\TestApp
    rmdir /s /q "%APPDATA%\TestApp" 2>nul
)

echo.
echo Test complete!
pause
