@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
timeout /t 3 /nobreak >nul
mkdir "%SCRIPT_DIR%text" 2>nul

endlocal
exit /b 0
