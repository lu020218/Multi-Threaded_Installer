@echo off
echo ========================================
echo Installer Workflow Test
echo ========================================
echo.

REM 设置路径
set BUILD_DIR=build\Release
set TEST_DATA=test_installer_data
set OUTPUT_DIR=test_installer_output

REM 清理旧数据
echo 1. Cleaning up old test data...
if exist %TEST_DATA% rmdir /s /q %TEST_DATA%
if exist %OUTPUT_DIR% rmdir /s /q %OUTPUT_DIR%
if exist test_installer.exe del test_installer.exe
echo    Done.
echo.

REM 创建测试数据（大文件以触发分块压缩）
echo 2. Creating test data (160MB to trigger block compression)...
mkdir %TEST_DATA%

REM 创建10个16MB的文件
for /L %%i in (1,1,10) do (
    echo    Creating file %%i of 10...
    fsutil file createnew %TEST_DATA%\test_file_%%i.dat 16777216 >nul
)
echo    Done.
echo.

REM 运行打包器
echo 3. Running packager...
%BUILD_DIR%\packager.exe --source %TEST_DATA% --output test_installer.exe --algorithm zstd --level 3
if errorlevel 1 (
    echo    ERROR: Packager failed!
    pause
    exit /b 1
)
echo    Done.
echo.

REM 检查安装包是否生成
if not exist test_installer.exe (
    echo    ERROR: Installer not generated!
    pause
    exit /b 1
)

REM 获取安装包大小
for %%A in (test_installer.exe) do set INSTALLER_SIZE=%%~zA
set /a INSTALLER_SIZE_MB=%INSTALLER_SIZE% / 1048576
echo    Installer size: %INSTALLER_SIZE% bytes (%INSTALLER_SIZE_MB% MB)
echo.

REM 运行诊断工具
echo 4. Running diagnostics...
%BUILD_DIR%\diagnose_installer.exe test_installer.exe
if errorlevel 1 (
    echo    ERROR: Diagnostics failed!
    pause
    exit /b 1
)
echo.

REM 运行安装器
echo 5. Running installer...
echo    Target directory: %OUTPUT_DIR%
test_installer.exe --destination %OUTPUT_DIR% --threads 4
if errorlevel 1 (
    echo    ERROR: Installer failed!
    pause
    exit /b 1
)
echo    Done.
echo.

REM 验证输出
echo 6. Verifying output...
if not exist %OUTPUT_DIR% (
    echo    ERROR: Output directory not created!
    pause
    exit /b 1
)

REM 计算输出文件数量
set FILE_COUNT=0
for /r %OUTPUT_DIR% %%f in (*) do set /a FILE_COUNT+=1

echo    Output files: %FILE_COUNT%
if %FILE_COUNT% LSS 10 (
    echo    WARNING: Expected 10 files, found %FILE_COUNT%
)
echo.

REM 比较文件大小
echo 7. Comparing file sizes...
set MATCH_COUNT=0
for /L %%i in (1,1,10) do (
    if exist %OUTPUT_DIR%\test_file_%%i.dat (
        for %%A in (%OUTPUT_DIR%\test_file_%%i.dat) do (
            if %%~zA EQU 16777216 (
                set /a MATCH_COUNT+=1
            ) else (
                echo    WARNING: File %%i size mismatch: %%~zA bytes
            )
        )
    ) else (
        echo    WARNING: File %%i not found
    )
)

echo    Matching files: %MATCH_COUNT% / 10
echo.

REM 总结
echo ========================================
echo Test Summary
echo ========================================
if %MATCH_COUNT% EQU 10 (
    echo Status: SUCCESS
    echo All files extracted correctly!
) else (
    echo Status: PARTIAL SUCCESS
    echo Some files may be missing or incorrect
)
echo.

REM 清理（可选）
echo Press any key to clean up test files, or Ctrl+C to keep them...
pause >nul
rmdir /s /q %TEST_DATA%
rmdir /s /q %OUTPUT_DIR%
del test_installer.exe
echo Cleanup complete.
